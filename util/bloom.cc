// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <iostream>
#include <vector>

#include "leveldb/filter_policy.h"
#include "leveldb/slice.h"

#include "util/hash.h"

namespace leveldb {

namespace {
static uint32_t BloomHash(const Slice& key) {
  return Hash(key.data(), key.size(), 0xbc9f1d34);
}

class BloomFilterPolicy : public FilterPolicy {
 public:
  explicit BloomFilterPolicy(std::vector<long> bits_per_key_per_level)
      : bits_per_key_per_level_(bits_per_key_per_level) {
    std::cout << "In bloom constructor" << std::endl;
    // We intentionally round down to reduce probing cost a little bit
    for (int i = 0; i < bits_per_key_per_level_.size(); i++) {
      std::cout << "Level " << i << " is " << bits_per_key_per_level[i] << std::endl;
      size_t k_ = static_cast<size_t>(bits_per_key_per_level_[i] * 0.69);
      if (k_ < 1) k_ = 1;
      if (k_ > 30) k_ = 30;

      k_per_level_.push_back(k_);
    }
  }

  const char* Name() const override { return "leveldb.BuiltinBloomFilter2"; }

  void CreateFilter(const Slice* keys, int n, std::string* dst,
                    int level) const override {
    // Compute bloom filter size (in both bits and bytes)
    // std::cout << "Creating filter of size" << n << std::endl;
    std::cout << "Creating filter of size " << n << "for level " << level << std::endl;
    size_t bits = n * bits_per_key_per_level_[level];

    // For small n, we can see a very high false positive rate.  Fix it
    // by enforcing a minimum bloom filter length.
    // if (bits < 64) bits = 64;

    size_t bytes = (bits + 7) / 8;
    bits = bytes * 8;

    const size_t init_size = dst->size();
    dst->resize(init_size + bytes, 0);
    dst->push_back(static_cast<char>(
        k_per_level_[level]));  // Remember # of probes in filter
    char* array = &(*dst)[init_size];
    for (int i = 0; i < n; i++) {
      // Use double-hashing to generate a sequence of hash values.
      // See analysis in [Kirsch,Mitzenmacher 2006].
      uint32_t h = BloomHash(keys[i]);
      const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
      for (size_t j = 0; j < k_per_level_[level]; j++) {
        const uint32_t bitpos = h % bits;
        array[bitpos / 8] |= (1 << (bitpos % 8));
        h += delta;
      }
    }
  }

  bool KeyMayMatch(const Slice& key, const Slice& bloom_filter) const override {
    // std::cout << "attempting to match" << key.ToString() << std::endl;
    const size_t len = bloom_filter.size();
    if (len < 2) return false;

    const char* array = bloom_filter.data();
    const size_t bits = (len - 1) * 8;

    // Use the encoded k so that we can read filters generated by
    // bloom filters created using different parameters.
    const size_t k = array[len - 1];
    if (k > 30) {
      // Reserved for potentially new encodings for short bloom filters.
      // Consider it a match.
      return true;
    }

    uint32_t h = BloomHash(key);
    const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
    for (size_t j = 0; j < k; j++) {
      const uint32_t bitpos = h % bits;
      if ((array[bitpos / 8] & (1 << (bitpos % 8))) == 0) return false;
      h += delta;
    }
    return true;
  }

 private:
  std::vector<long> bits_per_key_per_level_;
  std::vector<size_t> k_per_level_;
};
}  // namespace

const FilterPolicy* NewBloomFilterPolicy(std::vector<long> bits_per_key_per_level) {
  return new BloomFilterPolicy(bits_per_key_per_level);
}

}  // namespace leveldb

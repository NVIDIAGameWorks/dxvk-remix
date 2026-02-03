/*
* Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "util_fast_cache.h"

namespace dxvk {

  // A hash set that supports both positive entries (additions) and negative entries (removals).
  // Used for layer storage in the RtxOption system.
  // When merging layers, negative entries from a higher priority layer can remove values
  // that were added by a lower priority layer.
  // Entries are serialized with a '-' prefix for negative entries (e.g., "-0x1234567890ABCDEF").
  class HashSetLayer {
  public:
    bool operator==(const HashSetLayer& other) const {
      return m_positives == other.m_positives && m_negatives == other.m_negatives;
    }

    bool operator!=(const HashSetLayer& other) const {
      return !(*this == other);
    }

    bool empty() const {
      return m_positives.empty() && m_negatives.empty();
    }

    void clearAll() {
      m_positives.clear();
      m_negatives.clear();
    }

    // Add a hash to this layer (this layer wants to include this hash).
    // Removes from negatives if present (can't have both opinions).
    void add(XXH64_hash_t hash) {
      m_negatives.erase(hash);
      m_positives.insert(hash);
    }

    // Remove a hash from this layer (this layer wants to exclude this hash, overriding lower layers).
    // Removes from positives if present (can't have both opinions).
    void remove(XXH64_hash_t hash) {
      m_positives.erase(hash);
      m_negatives.insert(hash);
    }

    // Clear any opinion about this hash from this layer.
    // The hash will be neither added nor removed by this layer.
    void clear(XXH64_hash_t hash) {
      m_positives.erase(hash);
      m_negatives.erase(hash);
    }

    // Check if this layer has a positive entry for this hash.
    bool hasPositive(XXH64_hash_t hash) const {
      return m_positives.count(hash) > 0;
    }

    // Check if this layer has a negative entry for this hash.
    bool hasNegative(XXH64_hash_t hash) const {
      return m_negatives.count(hash) > 0;
    }

    // Returns the count of a hash in the resolved set (positives - negatives).
    // A hash is considered present (count = 1) if it's in positives AND NOT in negatives.
    size_t count(XXH64_hash_t hash) const {
      if (m_negatives.count(hash) > 0) {
        return 0;  // Negatives override positives
      }
      return m_positives.count(hash);
    }

    // Size of the positive set.
    size_t size() const { return m_positives.size(); }

    // Size of the negative set.
    size_t negativeSize() const { return m_negatives.size(); }

    // Iterator support - iterates only over positives (for compatibility with existing code).
    // TODO check if these can just be removed.
    auto begin() const { return m_positives.begin(); }
    auto end() const { return m_positives.end(); }
    auto begin() { return m_positives.begin(); }
    auto end() { return m_positives.end(); }

    // Find a hash in the resolved set (positives - negatives).
    // Returns end() if the hash is not in positives or if it is negated.
    auto find(XXH64_hash_t hash) const {
      if (m_negatives.count(hash) > 0) {
        return m_positives.end();  // Hash is negated, treat as not found
      }
      return m_positives.find(hash);
    }

    auto find(XXH64_hash_t hash) {
      if (m_negatives.count(hash) > 0) {
        return m_positives.end();  // Hash is negated, treat as not found
      }
      return m_positives.find(hash);
    }

    // Parse hash strings into this hash set.
    // Strings with '-' prefix are added as negative entries, others as positive.
    void parseFromStrings(const std::vector<std::string>& rawInput) {
      for (const auto& hashStr : rawInput) {
        if (hashStr.empty()) {
          continue;
        }
        
        // Trim leading/trailing whitespace
        size_t start = hashStr.find_first_not_of(" \t\n\r");
        size_t end = hashStr.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) {
          continue;  // String is all whitespace
        }
        std::string trimmed = hashStr.substr(start, end - start + 1);
        
        // Check if this is a negative entry (removal) with '-' prefix
        if (trimmed[0] == '-') {
          // Parse the hash value after the '-' prefix
          const XXH64_hash_t h = std::stoull(trimmed.substr(1), nullptr, 16);
          m_negatives.insert(h);
        } else {
          const XXH64_hash_t h = std::stoull(trimmed, nullptr, 16);
          m_positives.insert(h);
        }
      }
    }

    // Serialize this hash set to a string.
    // Positive entries are formatted as "0x...", negative entries as "-0x...".
    std::string toString() const {
      std::stringstream ss;
      
      // Collect positive entries for sorting
      std::vector<XXH64_hash_t> sortedPositives(m_positives.begin(), m_positives.end());
      std::sort(sortedPositives.begin(), sortedPositives.end());
      
      // Collect negative entries for sorting
      std::vector<XXH64_hash_t> sortedNegatives(m_negatives.begin(), m_negatives.end());
      std::sort(sortedNegatives.begin(), sortedNegatives.end());
      
      // Write positive entries first
      for (const auto& hash : sortedPositives) {
        if (ss.tellp() != std::streampos(0)) {
          ss << ", ";
        }
        ss << "0x" << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
      }
      
      // Write negative entries with '-' prefix
      for (const auto& hash : sortedNegatives) {
        if (ss.tellp() != std::streampos(0)) {
          ss << ", ";
        }
        ss << "-0x" << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
      }
      
      return ss.str();
    }

    // Compute which opinions were added compared to a saved hash set.
    // Returns a HashSetLayer containing only the newly added opinions:
    //   - Positive entries that are in current but not in saved
    //   - Negative entries that are in current but not in saved
    // Use this when exporting changes to a new config file.
    HashSetLayer computeAddedOpinions(const HashSetLayer& saved) const {
      HashSetLayer added;
      
      // Add newly added positive entries
      for (const auto& hash : m_positives) {
        if (saved.m_positives.count(hash) == 0) {
          added.m_positives.insert(hash);
        }
      }
      
      // Add newly added negative entries
      for (const auto& hash : m_negatives) {
        if (saved.m_negatives.count(hash) == 0) {
          added.m_negatives.insert(hash);
        }
      }
      
      return added;
    }

    // Compute the difference between this hash set and another (saved) hash set.
    // Returns a string showing what changed:
    //   +0x... = hash added to positives
    //   ~0x... = hash removed from positives
    //   +-0x... = negative entry added
    //   ~-0x... = negative entry removed
    std::string diffToString(const HashSetLayer& saved) const {
      std::stringstream ss;
      
      // Find hashes added to positives (in current but not in saved)
      std::vector<XXH64_hash_t> addedPositives;
      for (const auto& hash : m_positives) {
        if (saved.m_positives.count(hash) == 0) {
          addedPositives.push_back(hash);
        }
      }
      std::sort(addedPositives.begin(), addedPositives.end());
      
      // Find hashes removed from positives (in saved but not in current)
      std::vector<XXH64_hash_t> removedPositives;
      for (const auto& hash : saved.m_positives) {
        if (m_positives.count(hash) == 0) {
          removedPositives.push_back(hash);
        }
      }
      std::sort(removedPositives.begin(), removedPositives.end());
      
      // Find negatives added (in current but not in saved)
      std::vector<XXH64_hash_t> addedNegatives;
      for (const auto& hash : m_negatives) {
        if (saved.m_negatives.count(hash) == 0) {
          addedNegatives.push_back(hash);
        }
      }
      std::sort(addedNegatives.begin(), addedNegatives.end());
      
      // Find negatives removed (in saved but not in current)
      std::vector<XXH64_hash_t> removedNegatives;
      for (const auto& hash : saved.m_negatives) {
        if (m_negatives.count(hash) == 0) {
          removedNegatives.push_back(hash);
        }
      }
      std::sort(removedNegatives.begin(), removedNegatives.end());
      
      // Format output: +hash for added, ~hash for removed
      for (const auto& hash : addedPositives) {
        if (ss.tellp() != std::streampos(0)) {
          ss << ", ";
        }
        ss << "+0x" << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
      }
      
      for (const auto& hash : removedPositives) {
        if (ss.tellp() != std::streampos(0)) {
          ss << ", ";
        }
        ss << "~0x" << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
      }
      
      for (const auto& hash : addedNegatives) {
        if (ss.tellp() != std::streampos(0)) {
          ss << ", ";
        }
        ss << "+-0x" << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
      }
      
      for (const auto& hash : removedNegatives) {
        if (ss.tellp() != std::streampos(0)) {
          ss << ", ";
        }
        ss << "~-0x" << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
      }
      
      return ss.str();
    }

    // Merge a weaker (lower priority) layer into this accumulated result.
    // Called during resolution which iterates from highest to lowest priority.
    // Weaker layer's opinions only apply if this layer doesn't already have an opinion.
    void mergeFrom(const HashSetLayer& weaker) {
      for (const auto& hash : weaker.m_positives) {
        // Only add if we don't already have an opinion on this hash
        if (!hasPositive(hash) && !hasNegative(hash)) {
          m_positives.insert(hash);
        }
      }
      for (const auto& hash : weaker.m_negatives) {
        // Only remove if we don't already have an opinion on this hash
        if (!hasPositive(hash) && !hasNegative(hash)) {
          m_negatives.insert(hash);
        }
      }
    }

  private:
    fast_unordered_set m_positives;  // Hashes this layer adds
    fast_unordered_set m_negatives;  // Hashes this layer removes (overrides lower layers)
    
    // Allow RtxOption internals to access for resolution and UI display
    friend class RtxOptionImpl;
    template<typename T> friend class RtxOption;
  };


}

/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#include "../util/xxHash/xxhash.h"

namespace dxvk 
{

template<typename T>
class HashTable 
{
public:
  struct KeyPair
  {
    XXH64_hash_t hash;
    T data;
    KeyPair(const XXH64_hash_t& hash_, const T& data_)
      : hash(hash_)
      , data(data_)
    {}
  };

private:
  std::vector<KeyPair>* pTable;
  size_t numBuckets;

  size_t getBucket(const XXH64_hash_t& key) const
  {
    return key % numBuckets;
  }

public:
  class HashTableIterator 
  {
  private:
    std::vector<KeyPair>* pTable;
    size_t bucketIndex;
    size_t elementIndex;
    size_t numBuckets;
  public:
    HashTableIterator(std::vector<KeyPair>* table, size_t _numBuckets, size_t _bucketIndex, size_t _elementIndex)
    {
      pTable = table;
      bucketIndex = _bucketIndex;
      elementIndex = _elementIndex;
      numBuckets = _numBuckets;
    }
    bool operator!=(const HashTableIterator& rhs)
    {
      return (this->elementIndex != rhs.elementIndex) || (this->bucketIndex != rhs.bucketIndex) || (this->pTable != rhs.pTable);
    }
    KeyPair& operator*()
    {
      return pTable[bucketIndex][elementIndex];
    }
    void operator++()
    {
      if (pTable == nullptr)
      {
        return;
      }
      if (elementIndex == pTable[bucketIndex].size())
      {
        return;
      }
      elementIndex++;
      if (elementIndex == pTable[bucketIndex].size())
      {
        for(auto i = bucketIndex + 1; i < numBuckets; i++)
        {
          if (pTable[i].size() > 0)
          {
            bucketIndex = i;
            elementIndex = 0;
            return;
          }
        }
      }
    }
  };

  HashTable(size_t _numBuckets)
  {
    numBuckets = _numBuckets;
    pTable = new std::vector<KeyPair>[numBuckets];
  }
  ~HashTable()
  {
    delete[] pTable;
  }

  HashTableIterator begin()
  {
    for(size_t i = 0; i < numBuckets; i++)
    {
      if (pTable[i].size() > 0)
      {
        return HashTableIterator(pTable, numBuckets, i, 0);
      }
    }
    return HashTableIterator(nullptr, 0, 0, 0);
  }
  HashTableIterator end()
  {
    for(size_t i = numBuckets - 1; i > 0; i--)
    {
      if (pTable[i].size() > 0)
      {
        return HashTableIterator(pTable, numBuckets, i, pTable[i].size());
      }
    }
    return HashTableIterator(nullptr, 0, 0, 0);
  }
  const HashTableIterator begin() const
  {
    for(size_t i = 0; i < numBuckets; i++)
    {
      if (pTable[i].size() > 0)
      {
        return HashTableIterator(pTable, numBuckets, i, 0);
      }
    }
    return HashTableIterator(nullptr, 0, 0, 0);
  }
  const HashTableIterator end() const
  {
    for(size_t i = numBuckets - 1; i > 0; i--)
    {
      if (pTable[i].size() > 0)
      {
        return HashTableIterator(pTable, numBuckets, i, pTable[i].size());
      }
    }
    return HashTableIterator(nullptr, 0, 0, 0);
  }

  void clear()
  {
    delete[] pTable;
    pTable = new std::vector<KeyPair>[numBuckets];
  }

  size_t size()
  {
    size_t accumulator = 0;
    for (int i = 0; i < numBuckets; i++)
    {
      accumulator += pTable[i].size();
    }
    return accumulator;
  }

  T* insertElement(const XXH64_hash_t& key, const T& pNewData)
  {
    KeyPair k (key, pNewData);
    const uint32_t idx = pTable[getBucket(key)].size();
    pTable[getBucket(key)].push_back(k);
    return &pTable[getBucket(key)][idx].data;
  }

  void removeElement(const XXH64_hash_t& key)
  {
    size_t x = getBucket(key);

    typename std::vector<KeyPair>::iterator i;
    for (i = pTable[x].begin(); i != pTable[x].end(); i++)
    {
      if ((*i).hash == key)
        break;
    }

    if (i != pTable[x].end())
      pTable[x].erase(i);
  }

  const T* find(const XXH64_hash_t& key) const
  {
    size_t x = getBucket(key);

    typename std::vector<KeyPair>::iterator i;
    for (i = pTable[x].begin(); i != pTable[x].end(); i++)
    {
      if ((*i).hash == key)
        return &(*i).data;
    }

    return nullptr;
  }

  T* find(const XXH64_hash_t& key)
  {
    size_t x = getBucket(key);

    typename std::vector<KeyPair>::iterator i;
    for (i = pTable[x].begin(); i != pTable[x].end(); i++)
    {
      if ((*i).hash == key)
        return &(*i).data;
    }

    return nullptr;
  }
};

}  // namespace nvvk

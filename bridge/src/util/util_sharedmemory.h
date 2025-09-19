/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#ifndef UTIL_SHAREDMEMORY_H_
#define UTIL_SHAREDMEMORY_H_

#include <windows.h>
#include <memory.h>
#include <sstream>
#include <unordered_map>

#include "log/log.h"

namespace bridge_util {

  // Simple wrapper for windows shader memory via mapped files
  class SharedMemory {
  public:
    SharedMemory() {
    }
    SharedMemory(const std::string& name, const size_t size) {
      if (createSharedMemory(name, size)) {
        std::stringstream ss;
        ss << "Shared memory: [" << m_name << "] created and initialized successfully!";
        Logger::debug(ss.str());
      } else {
        std::stringstream ss;
        ss << "Shared memory: [" << name << "] failed during creation/initialization.";
        Logger::debug(ss.str());
        throw;
      }
    }
    SharedMemory(const SharedMemory& rhs) = delete;
    SharedMemory(SharedMemory&& rhs) {
      swap(rhs);
    }

    ~SharedMemory() {
      releaseSharedMemory();
    }

    void* data() const {
      return m_lpvMem;
    }

    size_t getSize() const {
      return m_size;
    }

    void swap(SharedMemory& rhs) {
      std::swap(m_name, rhs.m_name);
      std::swap(m_size, rhs.m_size);
      std::swap(m_lpvMem, rhs.m_lpvMem);
      std::swap(m_hMapObject, rhs.m_hMapObject);
    }

    // TODO: Implement malloc/free

  private:
    std::string m_name = "INVALID";
    size_t m_size = 0;
    LPVOID m_lpvMem = NULL;      // pointer to shared memory
    HANDLE m_hMapObject = NULL;  // handle to file mapping

    bool createSharedMemory(const std::string& name, const size_t size);
    void releaseSharedMemory();
  };

}

#endif // UTIL_SHAREDMEMORY_H_

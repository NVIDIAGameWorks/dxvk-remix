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
#include <windows.h> 
#include <memory.h> 

#include "util_common.h"
#include "util_guid.h"
#include "util_sharedmemory.h"

extern bridge_util::Guid gUniqueIdentifier;

namespace bridge_util {
  bool SharedMemory::createSharedMemory(const std::string& name, const size_t size) {
    m_name = gUniqueIdentifier.toString(name.c_str());
    m_size = size;

    // Create a named file mapping object
    m_hMapObject = CreateFileMapping(
      INVALID_HANDLE_VALUE, // use paging file
      NULL,                 // default security attributes
      PAGE_READWRITE,       // read/write access
      0,                    // size: high 32-bits
      m_size,               // size: low 32-bits
      m_name.c_str());      // name of map object

    if (m_hMapObject == NULL) {
      Logger::debug(format_string("The shared memory mapping object could not be created (error code %d)!", GetLastError()));
      return false;
    }

    // The first process to attach initializes memory
    const bool bIsInit = (GetLastError() != ERROR_ALREADY_EXISTS);

    // Get a pointer to the file-mapped shared memory
    m_lpvMem = MapViewOfFile(
      m_hMapObject,   // object to map view of
      FILE_MAP_WRITE, // read/write access
      0,              // high offset:  map from
      0,              // low offset:   beginning
      0);             // default: map entire file
    if (m_lpvMem == NULL) {
      Logger::debug(format_string("The shared memory map view could not be created (error code %d)!", GetLastError()));
      CloseHandle(m_hMapObject);
      return false;
    }

    // Initialize memory if this is the first process
    if (bIsInit) {
      Logger::info("Initializing new shared memory object.");
      memset(m_lpvMem, '\0', m_size);
    }

    return true;
  }

  void SharedMemory::releaseSharedMemory() {
    // Unmap shared memory from the process's address space
    auto ignore = UnmapViewOfFile(m_lpvMem);
    // Close the process's handle to the file-mapping object
    ignore = CloseHandle(m_hMapObject);
  }

}

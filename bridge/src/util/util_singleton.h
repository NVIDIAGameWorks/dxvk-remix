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
#pragma once

namespace bridge_util {

  template<typename T>
  class Singleton {
  public:
    static inline T& get() {
      // Using a pointer to workaround the expensive C++11 static object construction.
      // Per C standard, the static POD variables are default-initialized to zero,
      // and we'll use a NULL pointer as a flag to trigger the actual C++11 static
      // object construction. The method is thread-safe as long as platform's pointer
      // store/load op is thread-safe.
      static T* g_ptr;

      if (nullptr != g_ptr) {
        return *g_ptr;
      }

      // Construct the object and assign the pointer
      static T g_instance;
      g_ptr = &g_instance;

      return *g_ptr;
    }
  };

}

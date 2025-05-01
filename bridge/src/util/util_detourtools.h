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
#include <stdint.h>

namespace bridge_util {

  template<typename T>
  static bool DetourIsInjected(T* suspect) {
#if !defined(_M_IX86)
    return false;
#endif

    // Check if injected. Test for an immedite JMP on x86.
    return *reinterpret_cast<uint8_t*>(suspect) == 0xE9;
  }

  template<typename T>
  static T* DetourRetrieveOriginal(T* suspect) {
#if !defined(_M_IX86)
    return suspect;
#endif

    uint8_t* injected = reinterpret_cast<uint8_t*>(suspect);

    // Check if injected. Test for an immedite JMP on x86.
    if (*injected != 0xE9) {
      // Not injected
      return suspect;
    }

    // Recover original code header from detours-style injection
    uintptr_t injectionOffset = *reinterpret_cast<uintptr_t*>(injected + 1);
    // Detours has the return JMP right before the point of injection
    uint8_t* retJmp = injected + injectionOffset;

    // Check for a valid return JMP
    if (*retJmp != 0xE9) {
      // Was injected but likely not with Detours
      return suspect;
    }

    // Original code should be at -5 bytes before return JMP
    uint8_t* originalHead = retJmp - 5;

    return reinterpret_cast<T*>(originalHead);
  }

}

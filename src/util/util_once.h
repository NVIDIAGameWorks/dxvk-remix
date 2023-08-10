/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include <utility>

namespace dxvk {

  template <typename Func, typename Param >
  void doOnce(Func func, Param param) {
    static bool firstTime = true;
    if (firstTime) {
      func();
      firstTime = false;
    }
  }

  template <typename Func>
  void once(Func func) {
    doOnce(func, []() {});
  }

  // Use WHILE_TRUE() macro! May not work when used directly.
  template <typename CondEval, typename Func>
  static inline void whileTrue(CondEval&& cond, Func&& func) {
    static bool proceed = true;
    if (proceed && cond()) {
      func();
    } else {
      proceed = false;
    }
  }

  // Use ONCE_IF_FALSE() macro! May not work when used directly.
  template <typename CondEval, typename Func>
  static inline void onceIfFalse(CondEval&& cond, Func&& func) {
    static bool proceed = true;
    if (proceed && !cond()) {
      func();
      proceed = false;
    }
  }
}

// Be sure to make sure you're within dxvk namespace. Additionally, this macro is not thread safe.
#define ONCE(thing) once([=](){ thing; })
#define WHILE_TRUE(cond, thing) whileTrue([&]() -> bool { return cond; }, [&](){ thing; })
#define ONCE_IF_FALSE(cond, thing) onceIfFalse([&]() -> bool { return cond; }, [&](){ thing; })

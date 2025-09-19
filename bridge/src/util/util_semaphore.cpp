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
#include "util_semaphore.h"
#include "config/global_options.h"
#include "log/log.h"

#include <sstream>

namespace bridge_util {
  Result NamedSemaphore::wait() {
    return wait(GlobalOptions::getSemaphoreTimeout());
  }

  Result NamedSemaphore::wait(const DWORD timeoutMS) {
    // Note: WaitXXX commands decrement the semaphore value by 1
    DWORD dwWaitResult = WaitForSingleObject(ghSemaphore, timeoutMS);
    if (dwWaitResult == WAIT_OBJECT_0) {
      avail--;
      return Result::Success;
    } else if ((dwWaitResult != WAIT_TIMEOUT) || (timeoutMS == INFINITE)) {
      DWORD err = GetLastError();
      Logger::err(format_string("[%s] WaitForSingleObject failed (0x%x): 0x%x, avail = %d", baseName.c_str(), dwWaitResult, err, avail));
      return Result::Failure;
    } else {
      return Result::Timeout;
    }
  }

  void NamedSemaphore::release(LONG batchSize) {
    // Note: explicit increment by batch size, defaults to 1
    if (ReleaseSemaphore(ghSemaphore, batchSize, (LPLONG) &avail)) {
      // We increase the semaphore counter by the number of data or commands that were batched up,
      // so that on the server side we know how many to read until the queue is empty. Note that
      // the avail member isn't an accurate representation of the actual current counter value,
      // since increment or decrement usually happens in one process on the client or server and
      // then the reverse happens in the other process without updating the first one, and we can
      // only get the semaphore counter value on release but not on wait.
      avail += batchSize;
    } else {
      DWORD err = GetLastError();
      // We're filtering out errors coming from releasing the semaphore when it's already at full
      // count, because when batching up commands and data we asymmetrically increment the _reader_
      // semaphore by the batch size but the _writer_ semaphore only once at the beginning of the
      // batch, however on the server side when reading it will release the writer semaphore for
      // each item read, which will result in trying to release more often than needed.
      if (err != ERROR_TOO_MANY_POSTS) {
        Logger::err(format_string("[%s] ReleaseSemaphore failed: 0x%x, avail = %d", baseName.c_str(), err, avail));
      }
    }
  }
}

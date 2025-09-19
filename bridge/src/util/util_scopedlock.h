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

#include <type_traits>
#include "util_common.h"

#include <assert.h>

namespace bridge_util {

  /*
   * Base object of a syncable.
   * The derived object needs to implement all methods. The idea is that
   * the object can be synced on directly with lowest possible overhead
   * when its implementation is known (using *Impl() methods), or from
   * "the outside" using virtual methods. Basically the derived class
   * needs to implement *Impl() methods and then just call them from
   * overriden virtual lock/unlock methods.
   */
  struct Syncable {
    virtual void lock() = 0;
    virtual void unlock() = 0;

    void lockImpl() {
      assert(0 && "Please add lock() implementation to derived class!");
    }
    void unlockImpl() {
      assert(0 && "Please add unlock() implementation to derived class!");
    }
  };

  /*
   * ScopedLock implementation.
   * The syncable object must be derived from Syncable and implement
   * the neccessary methods. Direct lock mode will use *Impl()
   * non-virtual methods and basically will execute the underlying
   * implementation in-place. The non-direct mode will rely on
   * virtual methods and may have a slightly elevated cost.
   */
  template<typename SyncableType, bool DirectLock>
  struct ScopedLock: public NonCopyable {

    explicit ScopedLock(SyncableType* pObj): m_pObj { pObj } {
      static_assert(std::is_base_of_v<Syncable, SyncableType>,
        "The sync object type must be derived from Syncable!");

      assert(pObj && "Sync object is a nullptr!");

      if (DirectLock) {
        pObj->lockImpl();
      } else {
        pObj->lock();
      }
    }

    ~ScopedLock() {
      if (DirectLock) {
        m_pObj->unlockImpl();
      } else {
        m_pObj->unlock();
      }
    }

    SyncableType* m_pObj;
  };

  /*
   * No-op sync helper primitive.
   * Implements basic std::mutex methods.
   */
  struct nop_sync {
    inline void lock() {
    }
    inline bool try_lock() {
      return true;
    }
    inline void unlock() {
    }
  };

#define SCOPED_LOCK(pObj, DirectLock) \
  ScopedLock<std::remove_pointer<decltype(pObj)>::type, DirectLock> _(pObj)

} // namespace bridge_util

#ifdef WITH_MULTITHREADED_DEVICE

#define BRIDGE_DEVICE_LOCKGUARD() SCOPED_LOCK(this, true)
#define BRIDGE_PARENT_DEVICE_LOCKGUARD() SCOPED_LOCK(m_pDevice, false)

#else // WITH_MULTITHREADED_DEVICE

#define BRIDGE_DEVICE_LOCKGUARD()
#define BRIDGE_PARENT_DEVICE_LOCKGUARD() 

#endif // WITH_MULTITHREADED_DEVICE
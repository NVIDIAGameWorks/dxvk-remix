/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include <atomic>
#include <unordered_set>
#include <typeinfo>
#include <mutex>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

namespace dxvk {

  class DxvkDevice;

  /**
   * \brief Common Device Object
   * 
   * Every object that lives in Common Objects and may
   * need to hold a DxvkDevice reference must be derived
   * from this helper class.
   * Class implements a trivial object leak test and
   * methods to clean up internal circular references.
   */
  class CommonDeviceObject {
    static inline std::atomic_uint32_t s_checkValue;
#ifdef REMIX_DEVELOPMENT
    static inline
    std::unordered_set<const CommonDeviceObject*> s_checkSet;
    static inline std::mutex s_mutex;
#endif
    static inline std::atomic_bool s_isInitialized;

  public:
    CommonDeviceObject() = delete;

    explicit CommonDeviceObject(DxvkDevice* device)
    : m_device{device} {
      bool isInitialized = false;
      s_isInitialized.compare_exchange_weak(isInitialized, true);

      if (!isInitialized) {
        ::atexit([]() {
          if (s_checkValue == 0) {
            // We're clean
            return;
          }

          // Hopefully logger is still alive
          Logger::err(str::format("[", s_checkValue.load(),
            "] common device objects were not disposed of."));
#ifdef REMIX_DEVELOPMENT
          for (auto& obj : s_checkSet) {
            Logger::err(str::format("\tObject ", typeid(*obj).name(),
              " [", obj, "] is alive at exit."));
          }
#endif
        });
      }

      ++s_checkValue;
#ifdef REMIX_DEVELOPMENT
      std::lock_guard<std::mutex> lock(s_mutex);
      s_checkSet.insert(this);
#endif      
    }

    virtual ~CommonDeviceObject() {
      --s_checkValue;
#ifdef REMIX_DEVELOPMENT
      std::lock_guard<std::mutex> lock(s_mutex);
      s_checkSet.erase(this);
#endif
    }

    /**
     * \brief On destroy event
     * 
     * Called before object destruction and provides
     * the derived class with an opportunity to clean up
     * internal circular references to dxvk objects so that
     * the parent DxvkDevice can be destroyed. The common
     * offenders that hold a reference to DxvkDevice are:
     * DxvkContex, DxvkStagingDataAlloc.
     * If derived class has members of one of those classes,
     * these members MUST be cleaned up using the
     * onDestroy() method. The DxvkDevice object can NOT be
     * destroyed otherwise and will leak the entire set
     * of common objects.
     */
    virtual void onDestroy() {
    }

    DxvkDevice* device() const {
      return m_device;
    }

  protected:
    DxvkDevice* const m_device;
  };

}
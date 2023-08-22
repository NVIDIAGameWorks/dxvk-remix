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

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <assert.h>
#include "thread.h"
#include "util_env.h"
#include "dxvk_scoped_annotation.h"
#include "rc/util_rc_ptr.h"

namespace dxvk {
  class DxvkDevice;
  class DxvkContext;

  /**
    * \brief Implements a parallel rendering command processor.  
    *        Useful in scenarios where we need to process a queue of 
    *        items (T) in a parallel fashion.
    *
    *  typename T: Type of a single work item.
    * 
    *  Example usage: See RtxTextureManager
    */
  template<typename T>
  struct RenderProcessor {
    RenderProcessor() = delete;
    RenderProcessor(DxvkDevice* pDevice, const std::string& threadName)
      : m_threadName (threadName)
      , m_ctx(pDevice->createContext()) {
    }

    ~RenderProcessor() {
      onDestroy();
    }

    /**
      * \brief Called before destruction.
      */
    void onDestroy() {
      if (!m_stopped) {
        std::unique_lock<dxvk::mutex> lock(m_mutex);
        m_stopped.store(true);
        m_condOnAdd.notify_one();
      }

      if (m_thread.joinable()) {
        m_thread.join();
      }

      m_ctx = nullptr;
    }

    /**
      * \brief Starts the worker thread.
      */
    void start() {
      std::unique_lock<dxvk::mutex> lock(m_mutex);
      if (m_thread.joinable())
        return;

      m_ctx->beginRecording(m_ctx->getDevice()->createCommandList());

      m_thread = dxvk::thread([this] {
        env::setThreadName(m_threadName);

        threadFunc();
      });
    }

    /**
      * \brief Forces the pending work queue to flush.  
      *        Returns to caller when flush is complete.
      */
    void sync() {
      ScopedCpuProfileZone();

      std::unique_lock<dxvk::mutex> lock(m_mutex);

      if (!m_thread.joinable())
        return;

      m_condOnSync.wait(lock, [this] {
        return !m_itemsPending.load();
      });

      m_ctx->flushCommandList();
    }

    /**
      * \brief Adds an item to the work queue
      */
    void add(const T&& item) {
      ScopedCpuProfileZone();

      std::unique_lock<dxvk::mutex> lock(m_mutex);
      m_itemQueue.emplace(std::move(item));

      ++m_itemsPending;

      m_condOnAdd.notify_one();
    }

    /**
      * \brief Queries if the processor has been stopped.
      */
    bool hasStopped() const {
      return m_stopped;
    }

  protected:
    /**
      * \brief Work to be processed, must be handled by the implementation.
      */
    virtual void work(T& item, Rc<DxvkContext>& ctx) = 0;

    /**
      * \brief Conditions under which to wake worker, can be augmented by implementation.
      */
    virtual bool wakeWorkerCondition() {
      return !m_itemQueue.empty() || m_stopped.load();
    }

    std::atomic<uint32_t> m_itemsPending = { 0u };
    dxvk::condition_variable m_condOnAdd;

  private:
    dxvk::mutex m_mutex;
    std::atomic<bool> m_stopped = { false };
    dxvk::condition_variable m_condOnSync;
    dxvk::thread m_thread;
    std::string m_threadName;

    Rc<DxvkContext> m_ctx;

    std::queue<T> m_itemQueue;

    void threadFunc() {
      std::optional<T> optItem;

      try {
        while (!m_stopped.load()) {
          {
            std::unique_lock<dxvk::mutex> lock(m_mutex);

            if (optItem.has_value()) {
              if (--m_itemsPending == 0)
                m_condOnSync.notify_one();

              optItem.reset();
            }

            if (m_itemQueue.empty()) {
              m_condOnAdd.wait(lock, [this] {
                return wakeWorkerCondition();
              });
            }

            if (m_stopped.load())
              break;

            if (!m_itemQueue.empty()) {
              optItem = std::move(m_itemQueue.front());
              m_itemQueue.pop();
            }
          }

          if (!optItem.has_value())
            continue;

          ScopedCpuProfileZone();

          T& item = optItem.value();

          work(item, m_ctx);
        }
      } catch (const DxvkError& e) {
        Logger::err(str::format("Exception on, ", m_threadName, ", thread!"));
        Logger::err(e.message());
      }
    }
  };
} //dxvk
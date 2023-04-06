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
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <type_traits>
#include <future>
#include <assert.h>
#include "util_atomic_queue.h"
#include "util_env.h"
#include "util_math.h"
#include "util_fastops.h"
#include "sync/sync_spinlock.h"

namespace dxvk {
  /**
    * \brief Implements a async task scheduler, optimized
    *        for tasks of varying execution time using a
    *        work stealing algorithm.
    *
    *  NumThreads: How many threads to spawn (up to 255)
    *  NumTasksPerThread: Size of the task queue ring buffer
    *  WorkStealing: Enables the work stealing features of the scheduler
    *  LowLatency: Enables the low-latency mode where workers will spin instead of
    *              waiting for tasks on a conditional variable
    *  (ctor)workerName: Name given to threads with the pattern: workerName(N)
    * 
    *  Example usage:
    *   // Creates 1 thread, and uses it to return PI via a future
    *   WorkerThreadPool threadPool(1, "thread-pool-name");
    *   std::future<float> result = threadPool.Schedule([]{ return 3.14159265359f; });
    *   float pi = result.get();
    */
  template<size_t NumTasksPerThread, bool WorkStealing = true, bool LowLatency = true>
  class WorkerThreadPool {
    using Task = std::function<void()>;
    using Queue = AtomicQueue<Task, NumTasksPerThread>;
    using QueuePtr = std::unique_ptr<Queue>;

    struct Nop { };
    using OnAddCondition = std::conditional_t<LowLatency, Nop, dxvk::condition_variable>;
    using TaskMutex = std::conditional_t<LowLatency, Nop, dxvk::mutex>;

  public:
    WorkerThreadPool(uint8_t numThreads, const char* workerName = "Nameless Worker Thread") 
     : m_numThread(numThreads) {
      m_workerTasks.resize(m_numThread);
      m_workerThreads.resize(m_numThread);
      // Create the work queues first!  We need to create
      // then all since work stealing may access the other
      // queues.
      for (int i = 0; i < m_numThread; i++) {
        m_workerTasks[i] = std::make_unique<Queue>();
      }

      // Start the worker threads
      for (int i = 0; i < m_numThread; i++) {
        m_workerThreads[i] = std::thread([this, i, workerName] {
          env::setThreadName(str::format(workerName, "(", i, ")"));
          processWork(i);
        });
      }
    }

    ~WorkerThreadPool() {
      // Stop all the worker threads
      m_stopWork = true;

      if constexpr (!LowLatency) {
        m_condOnAdd.notify_all();
      }

      for (auto& worker : m_workerThreads) {
        worker.join();
      }
    }

    // Schedule a task to be executed by the thread pool
    template <uint8_t Affinity = 0xFF, typename F, typename... Args, typename R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>
    std::shared_future<R> Schedule(F&& f, Args&&... args) {
      std::function<R()> taskFunc = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
      std::shared_ptr<std::promise<R>> taskPromise = std::make_shared<std::promise<R>>();

      // Package up the user task, and wrap it with promise
      auto work = [taskFunc, taskPromise] {
        if constexpr (std::is_void_v<R>) {
          std::invoke(taskFunc);
          taskPromise->set_value();
        } else {
          taskPromise->set_value(std::invoke(taskFunc));
        }
      };

      // Add the task to the queue and notify a worker thread
      //  just distribute evenly to all threads for some mask denoted by Affinity.
      static size_t s_idx = 0;
      
      // Is the affinity mask valid?
      const uint8_t affinityMask = std::min(popcnt_uint8(Affinity), m_numThread);

      // Schedule work on the appropriate thread
      const uint32_t thread = fast::findNthBit(Affinity, (uint8_t) (s_idx++ % affinityMask));
      assert(thread < m_numThread);

      // Atomic queue is SPSC, so we don't need to take a lock here
      // since we know this will always be called from a single thread.
      if (!m_workerTasks[thread]->push(work)) {
        return std::shared_future<R>(); // the queue is full, return empty future
      }

      if constexpr (!LowLatency) {
        if constexpr (WorkStealing) {
          // Notify only one worker when workers can steal from the others
          m_condOnAdd.notify_one();
        } else {
          // Notify all workers when they cannot steal
          m_condOnAdd.notify_all();
        }
      }

      ++m_numTasks;

      return taskPromise->get_future();
    }

  private:
    void processWork(const uint32_t workerId) {
      while (true) {
        // Using a conditional wait in high-latency mode
        if constexpr (!LowLatency) {
          std::unique_lock<TaskMutex> lock(m_taskMutex);
          m_condOnAdd.wait(lock, [this] {
            return m_numTasks > 0 || m_stopWork.load();
          });
        }

        // Master halt
        if (m_stopWork) {
          return;
        }

        // Try executing a task from our queue
        if (executeTask(workerId))
          continue;

        if (WorkStealing) {
          // There's no work to do!
          // Steal work from other queues
          bool workStolen = false;
          for (uint32_t i = 1; i < m_numThread; i++) {
            const uint32_t victim = (workerId + i) % m_numThread;
            if (executeTask(victim)) {
              workStolen = true;
              break;
            }
          }

          // If nothing to steal, yield this thread
          if (!workStolen && LowLatency) {
            std::this_thread::yield();
          }
        }
      }
    }

    // True if front pop, False if back pop
    bool executeTask(const uint32_t workerId) {
      Task task;
      {
        // Since we're using an SPSC queue, we must take a lock when
        // popping, since we may be stealing (or be stolen from) by
        // another thread.
        std::unique_lock<sync::Spinlock> lock(m_threadMutex);

        if (!m_workerTasks[workerId]->pop(task)) {
          return false;
        }

        --m_numTasks;
      }

      // Execute the task
      if (task) {
        task();
        return true;
      }
      return false;
    }

    uint8_t m_numThread;

    std::atomic<bool> m_stopWork = false;

    // Used conditionally to wait for tasks in high-latency mode
    TaskMutex m_taskMutex;
    OnAddCondition m_condOnAdd;

    // Used to synchronize intra-thread stealing
    sync::Spinlock m_threadMutex;

    std::vector<std::thread> m_workerThreads;

    // We expect high volume of potentially small tasks via "Schedule" per-
    //  frame, and require extremely low overhead to hit the 100's of FPS.
    // Use a lock-free circular queue here for two reasons (profiled):
    //  1. Non-circular queue incurs allocation overhead thats unacceptable
    //  2. Use of mutex, and CVs, incur overhead thats unacceptable
    std::vector<QueuePtr> m_workerTasks;
    std::atomic_uint32_t m_numTasks;
  };
} //dxvk
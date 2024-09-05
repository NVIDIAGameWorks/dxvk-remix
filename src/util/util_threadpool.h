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
#include "util_bit.h"
#include "sync/sync_spinlock.h"

namespace dxvk {
  const size_t kLambdaStorageCapacity = 256;
  // Note: use up to 64 bytes for state
  const size_t kResultStorageCapacity = 256 - 64;

  template<size_t Capacity = kResultStorageCapacity, bool UseWait = false>
  struct Result {
    struct Nop { };
    using OnSetCondition = std::conditional_t<UseWait, dxvk::condition_variable, Nop>;
    using ResultMutex = std::conditional_t<UseWait, dxvk::mutex, Nop>;

    template<typename T>
    void set(T&& t) {
      static_assert(sizeof(T) <= Capacity,
          "Result object storage space overrun!");

      new(storage.data()) T(std::forward<T>(t));

      set();
    }

    void set() {
      if constexpr (UseWait) {
        std::unique_lock<dxvk::mutex> lock(mtx);
        hasResult = true;
        cond.notify_one();
      } else {
        hasResult = true;
      }
    }

    void get() {
#ifdef _DEBUG
      if (isDisposed) {
        throw DxvkError("Refusing to get a disposed result!");
      }
#endif

      if constexpr (UseWait) {
        if (!hasResult) {
          std::unique_lock<dxvk::mutex> lock(mtx);
          cond.wait(lock, [this] {
            return hasResult;
          });
        }
      } else {
        while (!hasResult) {
          std::this_thread::yield();
        }
      }

      hasResult = false;
      isDisposed = true;
    }

    template<typename T>
    T get() {
      get();
      return std::move(*reinterpret_cast<T*>(storage.data()));
    }

    void reset() {
      hasResult = false;
      isDisposed = false;
    }

    void cancel() {
      hasResult = false;
      isDisposed = true;
    }

    bool disposed() const {
      return isDisposed;
    }

  private:
    std::array<uint8_t, Capacity> storage;
    bool hasResult = false;
    bool isDisposed = false;

    OnSetCondition cond;
    mutable ResultMutex mtx;
  };

  using TaskId = uint32_t;
  template<typename ResultType> struct Future;

  struct Task {
    using LambdaStorage = std::array<uint8_t, kLambdaStorageCapacity>;
    using ThunkType = void(void*);
    using ThunkStorage = std::array<uint8_t, sizeof(uintptr_t)>;

    template<typename LambdaType, typename ResultType>
    Future<ResultType> capture(LambdaType&& lambda) {
      if constexpr (sizeof(LambdaType) > sizeof(lambdaStorage)) {
        char(*__type_size)[sizeof(LambdaType)] = 1;
        static_assert(false, "Task object storage space overrun!");
      }

      // Create lambda in-place
      new (lambdaStorage.data()) LambdaType(std::forward<LambdaType>(lambda));

      // We need to use a thunk to capture the actual lambda type
      captureThunk([this]() {
        auto& lambda = *reinterpret_cast<LambdaType*>(lambdaStorage.data());

        if (!result.disposed()) {
          if constexpr (!std::is_void_v<ResultType>) {
            result.set(lambda());
          } else {
            lambda();
            result.set();
          }
        }

        lambda.~LambdaType();
      });

      result.reset();

      return Future<ResultType>(*this);
    }

    void operator() () {
      dispatchThunk();
    }

    template<typename ResultType>
    ResultType getResult() {
      return result.get<ResultType>();
    }

    void getResult() {
      result.get();
    }

    void cancel() {
      result.cancel();
    }

    bool valid() const {
      return !result.disposed();
    }

  private:
    template<typename InvocableType>
    static inline void Thunk(void* thunkLambda) {
      (*static_cast<InvocableType*>(thunkLambda))();
    }

    template<typename TunkLambdaType>
    void captureThunk(TunkLambdaType&& thunkLambda) {
      new (thunkStorage.data()) TunkLambdaType(std::forward<TunkLambdaType>(thunkLambda));
      thunk = &Thunk<typename std::decay_t<TunkLambdaType>>;
    }

    void dispatchThunk() {
#ifdef _DEBUG
      if (!thunk) {
        throw DxvkError("Task thunk was not initialized!");
      }
#endif
      thunk(thunkStorage.data());
      thunk = nullptr;
    }

    alignas(64) LambdaStorage lambdaStorage;
    alignas(64) Result<kResultStorageCapacity> result;
    alignas(64) ThunkStorage thunkStorage;
    ThunkType* thunk = nullptr;
  };

  template<typename ResultType>
  struct Future {
    Future() = default;
    explicit Future(Task& task)
    : task { &task }
    { }

    ResultType get() const {
      ResultType r = task->getResult<ResultType>();
      task = nullptr;
      return r;
    }

    bool valid() const {
      return task != nullptr && task->valid();
    }

    void cancel() const {
      task->cancel();
      task = nullptr;
    }

  private:
    mutable Task* task = nullptr;
  };

  template<>
  struct Future<void> {
    Future() = default;
    explicit Future(Task& task)
    : task { &task } { }

    void get() const {
      task->getResult();
      task = nullptr;
    }

    bool valid() const {
      return task != nullptr && task->valid();
    }

    void cancel() const {
      task->cancel();
      task = nullptr;
    }

  private:
    mutable Task* task = nullptr;
  };

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
    *   Future<float> result = threadPool.Schedule([]{ return 3.14159265359f; });
    *   float pi = result.get();
    */
  template<size_t NumTasksPerThread, bool WorkStealing = true, bool LowLatency = true>
  class WorkerThreadPool {
    using Queue = AtomicQueue<TaskId, NumTasksPerThread>;
    using QueuePtr = std::unique_ptr<Queue>;

    struct Nop { };
    using OnAddCondition = std::conditional_t<LowLatency, Nop, dxvk::condition_variable>;
    using TaskMutex = std::conditional_t<LowLatency, Nop, dxvk::mutex>;

  public:
    WorkerThreadPool(uint8_t numThreads, const char* workerName = "Nameless Worker Thread") 
    : m_numThread(std::clamp(numThreads, (uint8_t)1u, (uint8_t)dxvk::thread::hardware_concurrency())) {
      // Note: round up to a closest power-of-two so we can use mask as modulo
      m_taskCount = 1 << (32 - bit::lzcnt(static_cast<uint32_t>(NumTasksPerThread * m_numThread) - 1));
      m_tasks.resize(m_taskCount);
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
        std::unique_lock<TaskMutex> lock(m_taskMutex);
        m_condOnAdd.notify_all();
      }

      for (auto& worker : m_workerThreads) {
        worker.join();
      }

      if (m_numTasks > 0) {
        for (auto& workerTasks : m_workerTasks) {
          TaskId taskId;
          while (workerTasks->pop(taskId)) {
            // Cancel the actual task job
            m_tasks[taskId].cancel();
            // Execute the task to dispatch the destructor
            m_tasks[taskId]();
            --m_numTasks;
          }
        }
      }

      assert(m_numTasks == 0 && "Tasks left in thread pool queue after destruction!");
    }

    // Schedule a task to be executed by the thread pool
    template <uint8_t Affinity = 0xFF, typename F, typename R = std::invoke_result_t<std::decay_t<F>>>
    Future<R> Schedule(F&& f) {
      // Is the affinity mask valid?
      const uint8_t affinityMask = std::min(popcnt_uint8(Affinity), m_numThread);

      // Schedule work on the appropriate thread
      const uint32_t thread = fast::findNthBit(Affinity, (uint8_t) (m_schedulerIndex++ % affinityMask));
      assert(thread < m_numThread);

      // Atomic queue is SPSC, so we don't need to take a lock here
      // since we know this will always be called from a single thread.

      Future<R> future;
      if (!m_workerTasks[thread]->isFull()) {
        // Get next task id
        TaskId taskId = m_taskId++ & (m_taskCount - 1);

        // Capture task lambda
        future = m_tasks[taskId].capture<F, R>(std::forward<F>(f));

        // Place task into queue
        m_workerTasks[thread]->push(std::move(taskId));

        if constexpr (!LowLatency) {
          std::unique_lock<TaskMutex> lock(m_taskMutex);
          if constexpr (WorkStealing) {
            // Notify only one worker when workers can steal from the others
            m_condOnAdd.notify_one();
          } else {
            // Notify all workers when they cannot steal
            m_condOnAdd.notify_all();
          }
        }

        ++m_numTasks;
      }

      return future;
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
      TaskId taskId;
      {
        // Since we're using an SPSC queue, we must take a lock when
        // popping, since we may be stealing (or be stolen from) by
        // another thread.
        std::unique_lock<sync::Spinlock> lock(m_threadMutex);

        if (!m_workerTasks[workerId]->pop(taskId)) {
          return false;
        }

        --m_numTasks;
      }

      // Execute the task
      m_tasks[taskId]();

      return true;
    }

    std::vector<Task> m_tasks;
    std::atomic<TaskId> m_taskId = 0;
    uint32_t m_taskCount;

    // Add the task to the queue and notify a worker thread
    //  just distribute evenly to all threads for some mask denoted by Affinity.
    size_t m_schedulerIndex = 0;

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
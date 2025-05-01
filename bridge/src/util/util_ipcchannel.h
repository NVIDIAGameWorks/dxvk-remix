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

#include "log/log.h"

#include "util_atomiccircularqueue.h"
#include "util_blockingcircularqueue.h"
#include "util_sharedmemory.h"

 // Helper struct that ties together everything needed to send commands and data and for synchronization
template<bridge_util::Accessor Accessor>
class IpcChannel {
  // Due to semaphore latency, BlockingCircularQueue is slower than AtomicCircularQueue
#ifdef USE_BLOCKING_QUEUE
  using CommandQueue = bridge_util::BlockingCircularQueue<Header, Accessor>;
#else
  using CommandQueue = bridge_util::AtomicCircularQueue<Header, Accessor>;
#endif
public:
  IpcChannel(const std::string& name,
             const size_t memSize,
             const size_t cmdQueueSize,
             const size_t dataQueueSize)
    : sharedMem(new bridge_util::SharedMemory(name + "Channel", memSize + kReservedSpace))
    , m_cmdMemSize(sizeof(Header)* cmdQueueSize + CommandQueue::getExtraMemoryRequirements())
    , m_dataMemSize(memSize - m_cmdMemSize)
    , serverDataPos(static_cast<int64_t*>(sharedMem->data()))
    , clientDataExpectedPos(serverDataPos + 1)
    , serverResetPosRequired(reinterpret_cast<bool*>(clientDataExpectedPos + 1))
    // Offsetting shared memory to account for 3 pointers used above
    , commands(new CommandQueue(name + "Command",
                                reinterpret_cast<void*>(
                                  reinterpret_cast<uintptr_t>(sharedMem->data()) +
                                  kReservedSpace),
                                m_cmdMemSize,
                                cmdQueueSize))
    , data(new bridge_util::DataQueue(name + "Data",
                                      Accessor,
                                      reinterpret_cast<void*>(
                                        reinterpret_cast<uintptr_t>(sharedMem->data()) +
                                        kReservedSpace + m_cmdMemSize),
                                      m_dataMemSize,
                                      dataQueueSize))
    , dataSemaphore(new bridge_util::NamedSemaphore(name + "Semaphore", 0, 1))
    , pbCmdInProgress(new std::atomic<bool>(false)) {
    // Check that we're leaving enough space.
    assert(m_cmdMemSize + m_dataMemSize <= sharedMem->getSize());
    // Initialize buffer override protection
    if constexpr (IS_WRITER(Accessor)) {
      *serverDataPos = -1;
      *clientDataExpectedPos = -1;
      *serverResetPosRequired = false;
    }
  }

  ~IpcChannel() {
    if (commands) delete commands;
    if (data) delete data;
    if (sharedMem) delete sharedMem;
    if (dataSemaphore) delete dataSemaphore;
  }

  size_t get_data_pos() const {
    return data->get_pos();
  }

  bridge_util::DataQueue::BaseType* get_data_ptr() const {
    return data->data();
  }

  bridge_util::SharedMemory* const   sharedMem;
  const size_t                       m_cmdMemSize;
  const size_t                       m_dataMemSize;
  int64_t*                           serverDataPos;
  int64_t*                           clientDataExpectedPos;
  bool*                              serverResetPosRequired;
  CommandQueue* const                commands;
  bridge_util::DataQueue* const      data;
  bridge_util::NamedSemaphore* const dataSemaphore;
  std::atomic<bool>* const           pbCmdInProgress;
  mutable std::mutex                 m_mutex;

  // Extra storage needed for data queue synchronization params
  static constexpr size_t kReservedSpace = align<size_t>(sizeof(*serverDataPos) +
    sizeof(*clientDataExpectedPos) + sizeof(*serverResetPosRequired), 64);
};
using WriterChannel = IpcChannel<bridge_util::Accessor::Writer>;
using ReaderChannel = IpcChannel<bridge_util::Accessor::Reader>;

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

#include "config/global_options.h"

#include "util_common.h"
#include "util_commands.h"
#include "util_circularbuffer.h"
#include "util_bridge_state.h"
#include "util_ipcchannel.h"
#include "util_singleton.h"
#include "../tracy/tracy.hpp"

extern bool gbBridgeRunning;

#define WAIT_FOR_SERVER_RESPONSE(func, value, uidVal) \
  { \
    const uint32_t timeoutMs = GlobalOptions::getAckTimeout(); \
    if (Result::Success != DeviceBridge::waitForCommand(Commands::Bridge_Response, timeoutMs, nullptr, true, uidVal)) { \
      Logger::err(func " failed with: no response from server."); \
      return value; \
    } \
  }

#define POP_BRIDGE_COMMAND_QUEUE() \
  { \
    DeviceBridge::pop_front(); \
  }

#define WAIT_FOR_OPTIONAL_SERVER_RESPONSE(func, value, uidVal) \
  { \
    if (GlobalOptions::getSendAllServerResponses()) { \
      WAIT_FOR_SERVER_RESPONSE(func, value, uidVal) \
      HRESULT res = (HRESULT) DeviceBridge::get_data(); \
      DeviceBridge::pop_front(); \
      return  res; \
    } else { \
      return D3D_OK; \
    } \
  } 

#define WAIT_FOR_OPTIONAL_CREATE_FUNCTION_SERVER_RESPONSE(func, value, uidVal) \
  { \
    if (GlobalOptions::getSendCreateFunctionServerResponses() || GlobalOptions::getSendAllServerResponses()) { \
      WAIT_FOR_SERVER_RESPONSE(func, value, uidVal) \
      HRESULT res = (HRESULT) DeviceBridge::get_data(); \
      DeviceBridge::pop_front(); \
      return  res; \
    } else { \
      return D3D_OK; \
    } \
  } 

using namespace bridge_util;

// Used structs over class enum for explicit type names while debugging
namespace BridgeId {
  struct _Bridge {
    virtual ~_Bridge() = 0; // Don't allow instantiation
  };
  struct Module : _Bridge {};
  struct Device : _Bridge {};
};
#define ASSERT_VALID_BRIDGE_ID(BRIDGE_ID) \
  static_assert(std::is_base_of<BridgeId::_Bridge, BRIDGE_ID>::value, "Must use valid BridgeId.");

template <typename BridgeId>
class Bridge {
  ASSERT_VALID_BRIDGE_ID(BridgeId);
  using DataT = uint32_t;
public:
  static void init(
    const std::string baseName,
    const size_t writerChannelMemSize, const size_t writerChannelCmdQueueSize,
    const size_t writerChannelDataQueueSize, const size_t readerChannelMemSize,
    const size_t readerChannelCmdQueueSize, const size_t readerChannelDataQueueSize);
  static inline const WriterChannel& getWriterChannel() {
    return *s_pWriterChannel;
  }
  static inline const ReaderChannel& getReaderChannel() {
    return *s_pReaderChannel;
  }
  
  //=========================//
  // Channel writing methods //
  //=========================//
  static inline bridge_util::Result begin_batch() {
    ZoneScoped;
  #ifdef USE_BLOCKING_QUEUE
    if (gbBridgeRunning) {
      return getWriterChannel().commands->begin_write_batch();
    }
  #endif
    return bridge_util::Result::Failure;
  }
  static inline size_t end_batch() {
    ZoneScoped;
  #ifdef USE_BLOCKING_QUEUE
    if (gbBridgeRunning) {
      return getWriterChannel().commands->end_write_batch();
    }
  #endif
    return 0;
  }

  //=========================//
  // Channel reading methods //
  //=========================//
  static inline const DataT& get_data() {
    ZoneScoped;
    size_t prevPos = get_data_pos();
    const Bridge::DataT& retval = getReaderChannel().data->pull();
    // Check if the server completed a loop
    if (*getReaderChannel().serverResetPosRequired && get_data_pos() < prevPos) {
      *getReaderChannel().serverResetPosRequired = false;
    }
    return retval;
  }

  static inline const DataT& get_data(void** obj) {
    ZoneScoped;
    size_t prevPos = get_data_pos();
    const Bridge::DataT& retval = getReaderChannel().data->pull(obj);
    // Check if the server completed a loop
    if ((*getReaderChannel().serverResetPosRequired) &&
        (get_data_pos() < prevPos)) {
      *getReaderChannel().serverResetPosRequired = false;
    }
    return retval;
  }

  template<typename T>
  static inline const size_t& copy_data(T& obj, bool checkSize = true) {
    ZoneScoped;
    size_t prevPos = get_data_pos();
    const DataT& retval = getReaderChannel().data->pull_and_copy(obj);

    if (checkSize) {
      assert((size_t) retval == sizeof(T) && "Size of source and target object does not match!");
      if ((size_t) retval != sizeof(T)) {
        Logger::err("DataQueue copy data: Size of source and target object does not match!");
      }
    }

    // Check if the server completed a loop
    if (*getReaderChannel().serverResetPosRequired && get_data_pos() < prevPos) {
      *getReaderChannel().serverResetPosRequired = false;
    }
    return retval;
  }

  static inline size_t get_data_pos() {
    ZoneScoped;
    return getReaderChannel().data->get_pos();
  }

  static inline bridge_util::Result begin_read_data() {
    ZoneScoped;
    if (gbBridgeRunning) {
      return getReaderChannel().data->begin_batch();
    }
    return bridge_util::Result::Failure;
  }

  static inline size_t end_read_data() {
    ZoneScoped;
    if (gbBridgeRunning) {
      return getReaderChannel().data->end_batch();
    }
    return 0;
  }

  static Header pop_front();
  static void syncDataQueue(size_t expectedMemUsage, bool posResetOnLastIndex = false);
  static bridge_util::Result ensureQueueEmpty();

  //=========================//
  // Channel waiting methods //
  //=========================//
  // Waits for a command to appear in the command queue. Upon success the command will NOT be removed from queue
  // and client MUST pull the command header manually by using pop_front() or pull() methods. The queue will enter
  // into an unrecoverable state otherwise.
  static bridge_util::Result waitForCommand(const Commands::D3D9Command& command = Commands::Bridge_Any,
                                            DWORD overrideTimeoutMS = 0,
                                            std::atomic<bool>* const pbEarlyOutSignal = nullptr, bool verifyUID = false, UID uidToVerify=0);
  // Waits for a command to appear in the command queue. Upon success the command will be removed from the queue
  // and discarded.
  static bridge_util::Result waitForCommandAndDiscard(const Commands::D3D9Command& command = Commands::Bridge_Any,
                                                      DWORD overrideTimeoutMS = 0,
                                                      std::atomic<bool>* const pbEarlyOutSignal = nullptr, bool verifyUID = false, UID uidToVerify = 0) {
    auto result = waitForCommand(command, overrideTimeoutMS, pbEarlyOutSignal, verifyUID, uidToVerify);
    if (bridge_util::Result::Success == result) {
      pop_front();
    }
    return result;
  }
  
  class Command {
  public:
    Command(const Commands::D3D9Command command) :
      Command(command, NULL) {
    }
    Command(const Commands::D3D9Command command, uintptr_t pHandle) :
      Command(command, pHandle, 0) {
    }
    Command(const Commands::D3D9Command command,
            uintptr_t pHandle,
            const Commands::Flags commandFlags);
    ~Command();

    inline void send_data(const DataT obj) {
      ZoneScoped;
      if (gbBridgeRunning) {
        syncDataQueue(1, false);
        const auto result = s_pWriterChannel->data->push(obj);
        if (RESULT_FAILURE(result)) {
          // For now just log when things go wrong, but could use some robustness improvements
          Logger::err("DataQueue send_data: Failed to send data!");
        }
      }
    }

    inline void send_data(const DataT size, const void* obj) {
      ZoneScoped;
      if (gbBridgeRunning) {
        size_t memUsed = (obj == nullptr) ? 1 : (align<size_t>(size, sizeof(DataT)) / sizeof(DataT)) + 1;
        syncDataQueue(memUsed, true);
        const auto result = s_pWriterChannel->data->push(size, obj);
        if (RESULT_FAILURE(result)) {
          // For now just log when things go wrong, but could use some robustness improvements
          Logger::err("DataQueue send_data: Failed to send data object!");
        }
      }
    }

    template<typename... Ts>
    inline void send_many(const Ts... objs) {
      ZoneScoped;
      if (gbBridgeRunning) {
        size_t count = sizeof...(Ts);
        syncDataQueue(count, false);
        const auto result = s_pWriterChannel->data->push_many(objs...);
        if (RESULT_FAILURE(result)) {
          // For now just log when things go wrong, but could use some robustness improvements
          Logger::err("DataQueue send_many: Failed to send multiple m_writerChanneldata items!");
        }
      }
    }

    inline uint8_t* begin_data_blob(const size_t size) {
      ZoneScoped;
      uint8_t* blobPacketPtr = nullptr;
      if (gbBridgeRunning) {
        size_t memUsed = align<size_t>(size, sizeof(DataT)) / sizeof(DataT) + 1;
        syncDataQueue(memUsed, true);
        const auto result = s_pWriterChannel->data->begin_blob_push(size, blobPacketPtr);
        if (RESULT_FAILURE(result)) {
          // For now just log when things go wrong, but could use some robustness improvements
          Logger::err("DataQueue begin_data_blob: Failed to begin sending a data blob!");
        }
      }
      return blobPacketPtr;
    }

    inline void end_data_blob() const {
      ZoneScoped;
      if (gbBridgeRunning) {
        s_pWriterChannel->data->end_blob_push();
      }
    }
    
    static inline size_t get_counter() {
      return s_cmdCounter;
    }

    static inline size_t get_uid() {
      return s_cmdUID;
    }

    static inline void reset_counter() {
      s_cmdCounter = 0;
    }
    
    static inline void print_data(std::string prefix, std::vector<Commands::D3D9Command>& commandList)       {
      for (const auto& currentCommand : commandList) {
        Logger::info(prefix + toString(currentCommand));
      }
    }

    static inline void print_writer_data_sent() {
      std::vector<Commands::D3D9Command> resultCommands = s_pWriterChannel->commands->getWriterQueueData();
      print_data("Command sent: ", resultCommands);
    }

    static inline void print_writer_data_received() {
      std::vector<Commands::D3D9Command> resultCommands = s_pWriterChannel->commands->getReaderQueueData();
      print_data("Command received: ", resultCommands);
    }

    static inline void print_reader_data_sent() {
      std::vector<Commands::D3D9Command> resultCommands = s_pReaderChannel->commands->getWriterQueueData();
      print_data("Command sent: ", resultCommands);
    }

    static inline void print_reader_data_received() {
      std::vector<Commands::D3D9Command> resultCommands = s_pReaderChannel->commands->getReaderQueueData();
      print_data("Command received: ", resultCommands);
    }

  private:
    const Commands::D3D9Command m_command;
    const uint32_t m_handle;
    const Commands::Flags m_commandFlags;
  };

private:
  Bridge() = delete;
  Bridge(const Bridge&) = delete;
  Bridge(const Bridge&&) = delete;
  static inline WriterChannel* s_pWriterChannel = nullptr;
  static inline ReaderChannel* s_pReaderChannel = nullptr;
  static inline int32_t        s_curBatchStartPos = -1;
  static inline size_t         s_cmdCounter = 0;
  // UIDs are assigned to commands to tag the responses from server to allow misorder responses to be handled correctly 
  static inline UID s_cmdUID = 0;
#if defined(REMIX_BRIDGE_CLIENT)
  static constexpr char kWriterChannelName[] = "Client2Server";
  static constexpr char kReaderChannelName[] = "Server2Client";
#elif defined(REMIX_BRIDGE_SERVER)
  static constexpr char kWriterChannelName[] = "Server2Client";
  static constexpr char kReaderChannelName[] = "Client2Server";
#endif
};

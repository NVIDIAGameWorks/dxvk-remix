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
#include "util_bridgecommand.h"
#include "log/log_strings.h"

namespace {
  DWORD get_default_timeout() {
    const auto timeout = GlobalOptions::getCommandTimeout();
    const auto retries = GlobalOptions::getCommandRetries();
    const auto default = timeout * retries;
    // Catch overflow and return infinite in that case
    return ((timeout != 0) && (default / timeout != retries)) ? INFINITE : default;
  }
}

#define DECL_BRIDGE_FUNC(RETURN_T, NAME, ...) \
  template<typename BridgeId> \
  RETURN_T Bridge<BridgeId>::NAME(__VA_ARGS__)

DECL_BRIDGE_FUNC(void, init,
    const std::string baseName,
    const size_t writerChannelMemSize, const size_t writerChannelCmdQueueSize,
    const size_t writerChannelDataQueueSize, const size_t readerChannelMemSize,
    const size_t readerChannelCmdQueueSize, const size_t readerChannelDataQueueSize) {
  static bool bIsInit = false;
  if (bIsInit) {
    Logger::warn("Re-Init'ing Bridge type. May be sign of problem code.");
    return;
  }
  s_pWriterChannel = new WriterChannel(
    baseName + kWriterChannelName,
    writerChannelMemSize, writerChannelCmdQueueSize, writerChannelDataQueueSize);
  s_pReaderChannel = new ReaderChannel(
    baseName + kReaderChannelName,
    readerChannelMemSize, readerChannelCmdQueueSize, readerChannelDataQueueSize);
  bIsInit = true;
}

DECL_BRIDGE_FUNC(void, syncDataQueue, size_t expectedMemUsage, bool posResetOnLastIndex) {
  int serverCount = *s_pWriterChannel->serverDataPos;
  size_t currClientDataPos = s_pWriterChannel->get_data_pos();
  size_t expectedClientDataPos = currClientDataPos + ((expectedMemUsage != 0) ? expectedMemUsage : 1) - 1;
  size_t totalSize = s_pWriterChannel->data->get_total_size();

  auto handleOverwriteCondition = [&]() {
    // Below variable is set to let the server know that a particular position
    // in the queue not yet accessed by it is going to be used
    *s_pWriterChannel->clientDataExpectedPos = s_curBatchStartPos - 1;
    Logger::warn("Data Queue overwrite condition triggered");
    // Check to see if there is even enough space to ever succeed in pushing all the data
    if ((expectedMemUsage + (currClientDataPos >= s_curBatchStartPos ? currClientDataPos - s_curBatchStartPos : currClientDataPos + totalSize - s_curBatchStartPos)) > totalSize) {
      Logger::errLogMessageBoxAndExit(std::string(logger_strings::OutOfBufferMemory) + std::string(logger_strings::OutOfBufferMemory1) + logger_strings::bufferNameToOption(s_pWriterChannel->data->getName()));
    }
    // Wait for the server to access the data at the above postion
    const auto maxRetries = GlobalOptions::getCommandRetries();
    size_t numRetries = 0;
    Logger::warn("Waiting on server to process enough data from data queue to prevent overwrite...");
    while (RESULT_FAILURE(s_pWriterChannel->dataSemaphore->wait()) && numRetries++ < maxRetries) {
    }
    if (numRetries >= maxRetries) {
      Logger::err("Max retries reached waiting on the server to process enough data to prevent a overwrite!");
    }
    *s_pWriterChannel->clientDataExpectedPos = -1;
    *s_pWriterChannel->serverResetPosRequired = false;
    Logger::info("DataQueue overwrite condition resolved");
  };

  if (expectedClientDataPos >= totalSize) {
    if (*s_pWriterChannel->serverResetPosRequired == true) {
      // Double Overflow Condition Detected, mitigate by stalling and waiting for a response
      handleOverwriteCondition();
    }
    if (posResetOnLastIndex) {
      // Reset index pos to 0 if the size is larger than the remaining buffer
      expectedClientDataPos = expectedMemUsage - 1;
    } else {
      // Evaluate the respective pos when the end of the queue is reached
      expectedClientDataPos = expectedClientDataPos - totalSize;
    }
    // Below variable is set when the server needs to complete a loop to get to
    // the client's expected position. When this is set on pull we check if
    // m_pos is reset and toggle this variable if it is
    *s_pWriterChannel->serverResetPosRequired = true;
  }

  /*
    * overwrite conditions
    * 1. client < server, expectedClient >= server
    * 2. client > server, expectedClient >= server, expectedClient < client
    */
  if (expectedClientDataPos >= serverCount &&
      ((s_curBatchStartPos < serverCount)
       || (s_curBatchStartPos > serverCount && expectedClientDataPos < s_curBatchStartPos)
       || ((s_curBatchStartPos <= serverCount) && *s_pWriterChannel->serverResetPosRequired))) {
    handleOverwriteCondition();
  }
}

DECL_BRIDGE_FUNC(Header, pop_front) {
  ZoneScoped;
  Result result;
  // No retries, but wait the same amount of time
  const auto response = getReaderChannel().commands->pull(result, get_default_timeout());
  if (RESULT_FAILURE(result)) {
    // For now just log when things go wrong, but could use some robustness improvements
    Logger::err("CommandQueue get_response: Failed to retrieve the command response!");
  }
  return response;
}

DECL_BRIDGE_FUNC(bridge_util::Result, ensureQueueEmpty) {
  if (getReaderChannel().commands->isEmpty()) {
    return bridge_util::Result::Success;
  }

  const uint32_t maxAttempts = GlobalOptions::getCommandRetries();
  uint32_t attemptNum = 0;
  do {
    Result result;
    getReaderChannel().commands->peek(result, 1);

    if (result != Result::Timeout) {
      // Give the server some time to process the commands
      Sleep(8);
    } else {
      // Timeout from peek() means the queue is empty
      return bridge_util::Result::Success;
    }
  } while (attemptNum++ <= maxAttempts && gbBridgeRunning);

  return bridge_util::Result::Timeout;
}

DECL_BRIDGE_FUNC(bridge_util::Result, waitForCommand, const Commands::D3D9Command& command,
                                                      DWORD overrideTimeoutMS,
                                                      std::atomic<bool>* const pbEarlyOutSignal, bool verifyUID, UID uidToVerify) {
  ZoneScoped;
  DWORD peekTimeoutMS = overrideTimeoutMS > 0 ? overrideTimeoutMS : GlobalOptions::getCommandTimeout();
  uint32_t maxAttempts = GlobalOptions::getCommandRetries();
#ifdef ENABLE_WAIT_FOR_COMMAND_TRACE
  if (command != Commands::Any) {
    Logger::trace(format_string("Waiting for command %s for %d ms up to %d times...", Commands::toString(command).c_str(), peekTimeoutMS, maxAttempts));
  }
#endif
#if defined(_DEBUG) || defined(DEBUGOPT)
  if (GlobalOptions::getLogAllCommands()) {
    Logger::info("waitForCommand Command:" + toString(command) + (verifyUID ? " UID: " + std::to_string(uidToVerify) : ""));
  }
#endif
  bool infiniteRetries = false;
  bool bEarlyOut = false;
  uint32_t attemptNum = 0;
  do {
    Result result;
    Header header = getReaderChannel().commands->peek(result, peekTimeoutMS, pbEarlyOutSignal);

    switch (result) {

    case Result::Success:
    {
      bool uidVerified = true;
      if (verifyUID) {
        if (header.pHandle != uidToVerify) {
          uidVerified = false;
        }
      }
      if ((command == Commands::Bridge_Any) || (header.command == command) && uidVerified) {
#ifdef ENABLE_WAIT_FOR_COMMAND_TRACE
        if (command != Commands::Bridge_Any) {
          Logger::trace(format_string("...success, command %s received!", Commands::toString(command).c_str()));
        }
#endif
        return Result::Success;
      } else {
#if defined(_DEBUG) || defined(DEBUGOPT)
        if (GlobalOptions::getLogAllCommands()) {
          Logger::info(format_string("Different instance of a command detected: %s with UID: %s , Expected: %s with UID: %s. ", Commands::toString(header.command).c_str(), std::to_string(header.pHandle).c_str(),
                                     Commands::toString(command).c_str(), std::to_string(uidToVerify).c_str()));
        }
#endif
        // If we see the incorrect command, we want to give the other side of
        // the bridge ample time to make an attempt to process it first
        Sleep(peekTimeoutMS);
      }
      break;
    }

    case Result::Timeout:
    {
      if (GlobalOptions::getInfiniteRetries()) {
        // Infinite retries requested, the application might be alt-tabbed and sleeping, and so we need to wait too.

        // Set timeout for consecutive peeks to 1ms to relieve spin-waits.
        peekTimeoutMS = 1;
        // Decrease the attempt counter so it won't overrun maxAttempts in case it did not capture infinite retries.
        if (attemptNum > 0) {
          --attemptNum;
        }
        // Set the flag so that the consecutive peek 1ms-timeout would not generate a failure in case if
        // infinite retries are revoked in the process.
        infiniteRetries = true;

        // Sleep for default OS period
        Sleep(1);
      } else if (infiniteRetries) {
        // A timeout in infinite retries loop but infinite retries have been revoked (app restored from alt-tab).

        // Restore peek timeout interval and continue.
        peekTimeoutMS = overrideTimeoutMS > 0 ? overrideTimeoutMS : GlobalOptions::getCommandTimeout();
        // Drop the flag - we're in the normal loop now.
        infiniteRetries = false;
      }
      Logger::trace(format_string("Peek timeout while waiting for command: %s.", Commands::toString(command).c_str()));
      break;
    }

    case Result::Failure:
    {
      Logger::trace(format_string("Peek failed while waiting for command: %s.", Commands::toString(command).c_str()));
      return Result::Failure;
    }

    }
    if (pbEarlyOutSignal) {
      bEarlyOut = pbEarlyOutSignal->load();
    }
  } while (!bEarlyOut &&
            attemptNum++ <= maxAttempts &&
            gbBridgeRunning);
  return Result::Timeout;
}

#define DECL_COMMAND_FUNC(RETURN_T, NAME, ...) \
  template<typename BridgeId> \
  RETURN_T Bridge<BridgeId>::Command::NAME(__VA_ARGS__)

DECL_COMMAND_FUNC(,Command,const Commands::D3D9Command command,
                           uintptr_t pHandle,
                           const Commands::Flags commandFlags)
  : m_command(command)
  , m_handle((uint32_t) (size_t) pHandle)
  , m_commandFlags(commandFlags) {
  // If the assert or exception gets triggered it means that there is more than one Command
  // instance in a function or command block with overlapping object lifecycles. Only one instance
  // can be alive at a time to ensure data integrity on the command and data buffers. To resolve
  // this issue I recommend enclosing the Command object in its own scope block, and make
  // sure there is no command nesting happening either.

#if defined(_DEBUG) || defined(DEBUGOPT)
  if (GlobalOptions::getLogAllCommands()) {
#ifdef REMIX_BRIDGE_CLIENT
    Logger::info("Requesting: " +toString(command) + " UID: " + std::to_string(s_cmdUID));
#else
    Logger::info("Responding: " + toString(command) + " UID: " + std::to_string(pHandle));
#endif
  }
#endif

#ifdef REMIX_BRIDGE_CLIENT
  s_pWriterChannel->m_mutex.lock();
#endif

  assert(!s_pWriterChannel->pbCmdInProgress->load());
  if (s_pWriterChannel->pbCmdInProgress->load()) {
    Logger::errLogMessageBoxAndExit(logger_strings::MultipleActiveCommands);
  }
  // Only start a data batch if the bridge is actually enabled, otherwise this becomes a no-op
  if (gbBridgeRunning) {
    s_pWriterChannel->data->begin_batch();
  }
  s_pWriterChannel->pbCmdInProgress->store(true);
  s_curBatchStartPos = (int32_t) s_pWriterChannel->data->get_pos();
  s_cmdCounter++;
  if (gbBridgeRunning) {
    // Send command id as part of data queue for everycommand from client to server
#ifdef REMIX_BRIDGE_CLIENT
      syncDataQueue(1, false);
      const auto result = s_pWriterChannel->data->push((UINT)s_cmdUID);
#if defined(_DEBUG) || defined(DEBUGOPT)
      if (GlobalOptions::getLogAllCommands()) {
        Logger::info("Pushed UID: " + std::to_string(s_cmdUID));
      }
#endif
      if (RESULT_FAILURE(result)) {
        // For now just log when things go wrong, but could use some robustness improvements
        Logger::err("DataQueue send_data: Failed to send data!");
      }
#endif
  }
}

DECL_COMMAND_FUNC(,~Command) {
  // Only actually send the command if the bridge is enabled, otherwise this becomes a no-op
  if (gbBridgeRunning) {
    s_pWriterChannel->data->end_batch();
    s_curBatchStartPos = -1;
    uint32_t numRetries = 0;
    Result result;
    // We check if the bridge is enabled for each loop iteration in case it
    // was disabled externally by the server process exit callback.
    do {
      result = s_pWriterChannel->commands->push({ m_command, m_commandFlags, (uint32_t) s_pWriterChannel->data->get_pos(), m_handle });
#if defined(_DEBUG) || defined(DEBUGOPT)
      if (GlobalOptions::getLogAllCommands()) {
        Logger::info("Pushed: " + toString(m_command));
      }
#endif
    } while (
          RESULT_FAILURE(result)
      && numRetries++ < GlobalOptions::getCommandRetries()
      && gbBridgeRunning
#ifdef REMIX_BRIDGE_CLIENT
      && BridgeState::getServerState_NoLock() == BridgeState::ProcessState::Running
#endif
    );
#ifdef REMIX_BRIDGE_CLIENT
    if (BridgeState::getServerState_NoLock() >= BridgeState::ProcessState::DoneProcessing) {
      Logger::warn(format_string("The command %s will not be sent; Server is in the process of or has already shut down. Turning bridge off.", Commands::toString(m_command).c_str()));
      gbBridgeRunning = false;;
    } else
#endif
      if (RESULT_FAILURE(result) && gbBridgeRunning) {
        Logger::err(format_string("The command %s could not be successfully sent, turning bridge off and falling back to client rendering!", Commands::toString(m_command).c_str()));
        gbBridgeRunning = false;
      } else if (RESULT_SUCCESS(result) && numRetries > 1) {
        std::string command = Commands::toString(m_command);
        Logger::debug(format_string("The command %s took %d retries (%d ms)!", command.c_str(), numRetries, numRetries * GlobalOptions::getCommandTimeout()));
      }
  }
  s_pWriterChannel->pbCmdInProgress->store(false);
#ifdef REMIX_BRIDGE_CLIENT
  ++s_cmdUID;
  s_pWriterChannel->m_mutex.unlock();
#endif
}

template class Bridge<BridgeId::Module>;
template class Bridge<BridgeId::Device>;
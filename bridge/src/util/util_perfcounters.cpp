/*
 * Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "util_perfcounters.h"

#include "util_common.h"
#include "util_commands.h"
#include "log/log.h"

namespace bridge_util {
  namespace perf {
    std::atomic<uint32_t> g_intervalFrames { 0 };

    static_assert(Commands::IDirect3DQuery9_GetData < kMaxCommands, "perf::kMaxCommands is too small for the command enum");

    namespace {
#if defined(REMIX_BRIDGE_CLIENT)
      constexpr char kSide[] = "client";
#else
      constexpr char kSide[] = "server";
#endif
      constexpr uint32_t kCreateFailureReportFrames = 300;
      constexpr uint64_t kCreateFailuresLoggedIndividually = 20;
      constexpr size_t kTopCommands = 12;
      constexpr size_t kTopBlocking = 8;

      // Static storage zero-initializes these before any constructor runs.
      std::atomic<uint32_t> s_cmdCount[kMaxCommands];
      std::atomic<uint32_t> s_respWaitCount[kMaxCommands];
      std::atomic<uint64_t> s_respWaitTicks[kMaxCommands];
      std::atomic<uint32_t> s_droppedCount[kMaxCommands];
      std::atomic<uint64_t> s_records;
      std::atomic<uint64_t> s_commands;
      std::atomic<uint64_t> s_dataWords;
      std::atomic<uint64_t> s_cmdTicks;
      std::atomic<uint64_t> s_uploadBytes;
      std::atomic<uint64_t> s_presentWaits;
      std::atomic<uint64_t> s_presentWaitTicks;
      std::atomic<uint32_t> s_lastSent;
      std::atomic<uint64_t> s_queryCalls[size_t(QueryPath::Count)];
      std::atomic<uint64_t> s_querySignalled[size_t(QueryPath::Count)];
      std::atomic<uint64_t> s_asyncCreateFailures;

      struct QueueSnapshot {
        uint64_t waits = 0;
        uint64_t spinTicks = 0;
        uint64_t blockTicks = 0;
        uint64_t blocks = 0;
        uint64_t wakesSent = 0;

        static QueueSnapshot take(const QueueWaitStats& stats) {
          QueueSnapshot snapshot;
          snapshot.waits = stats.waits.load(std::memory_order_relaxed);
          snapshot.spinTicks = stats.spinTicks.load(std::memory_order_relaxed);
          snapshot.blockTicks = stats.blockTicks.load(std::memory_order_relaxed);
          snapshot.blocks = stats.blocks.load(std::memory_order_relaxed);
          snapshot.wakesSent = stats.wakesSent.load(std::memory_order_relaxed);
          return snapshot;
        }

        QueueSnapshot operator-(const QueueSnapshot& other) const {
          QueueSnapshot delta;
          delta.waits = waits - other.waits;
          delta.spinTicks = spinTicks - other.spinTicks;
          delta.blockTicks = blockTicks - other.blockTicks;
          delta.blocks = blocks - other.blocks;
          delta.wakesSent = wakesSent - other.wakesSent;
          return delta;
        }
      };

      // Only the presenting thread touches this state.
      struct FrameState {
        bool started = false;
        uint32_t frames = 0;
        int64_t intervalStart = 0;
        QueueSnapshot writerFull;
        QueueSnapshot readerEmpty;
        bool cpuValid = false;
        DWORD cpuThreadId = 0;
        uint64_t cpu100ns = 0;
        uint32_t createReportFrames = 0;
        uint64_t createFailuresReported = 0;
      } s_frame;

      double ticksPerUs() {
        static const double value = [] {
          LARGE_INTEGER frequency;
          QueryPerformanceFrequency(&frequency);
          return double(frequency.QuadPart) / 1e6;
        }();
        return value;
      }

      double toUs(uint64_t ticks) {
        return double(ticks) / ticksPerUs();
      }

      bool threadCpu100ns(uint64_t& out) {
        FILETIME creationTime;
        FILETIME exitTime;
        FILETIME kernelTime;
        FILETIME userTime;
        if (!GetThreadTimes(GetCurrentThread(), &creationTime, &exitTime, &kernelTime, &userTime)) {
          return false;
        }
        const auto toU64 = [](const FILETIME& t) { return (uint64_t(t.dwHighDateTime) << 32) | uint64_t(t.dwLowDateTime); };
        out = toU64(kernelTime) + toU64(userTime);
        return true;
      }

      std::string shortName(uint32_t command) {
        std::string name = Commands::toString(static_cast<Commands::D3D9Command>(command));
        static constexpr char kPrefix[] = "IDirect3D";
        if (name.rfind(kPrefix, 0) == 0) {
          name = name.substr(sizeof(kPrefix) - 1);
        }
        return name;
      }

      enum CommandClass { kSetters, kDraws, kUploads, kLocks, kCreates, kDestroys, kGets, kOther, kClassCount };
      constexpr const char* kClassNames[kClassCount] = { "setters", "draws", "uploads", "locks", "creates", "destroys", "gets", "other" };

      CommandClass classify(uint32_t command) {
        const std::string name = Commands::toString(static_cast<Commands::D3D9Command>(command));
        const size_t separator = name.find('_');
        const std::string method = (separator == std::string::npos) ? name : name.substr(separator + 1);
        const auto starts = [&method](const char* prefix) { return method.rfind(prefix, 0) == 0; };
        if (starts("Set")) {
          return kSetters;
        }
        if (starts("Draw")) {
          return kDraws;
        }
        if (starts("Unlock") || starts("UpdateSurface") || starts("UpdateTexture")) {
          return kUploads;
        }
        if (starts("Lock")) {
          return kLocks;
        }
        if (starts("Create")) {
          return kCreates;
        }
        if (starts("Destroy")) {
          return kDestroys;
        }
        if (starts("Get")) {
          return kGets;
        }
        return kOther;
      }

      void resetCounters() {
        for (uint32_t i = 0; i < kMaxCommands; ++i) {
          s_cmdCount[i].store(0, std::memory_order_relaxed);
          s_respWaitCount[i].store(0, std::memory_order_relaxed);
          s_respWaitTicks[i].store(0, std::memory_order_relaxed);
          s_droppedCount[i].store(0, std::memory_order_relaxed);
        }
        s_commands.store(0, std::memory_order_relaxed);
        s_records.store(0, std::memory_order_relaxed);
        s_dataWords.store(0, std::memory_order_relaxed);
        s_cmdTicks.store(0, std::memory_order_relaxed);
        s_uploadBytes.store(0, std::memory_order_relaxed);
        s_presentWaits.store(0, std::memory_order_relaxed);
        s_presentWaitTicks.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < size_t(QueryPath::Count); ++i) {
          s_queryCalls[i].store(0, std::memory_order_relaxed);
          s_querySignalled[i].store(0, std::memory_order_relaxed);
        }
      }

      void startInterval(const QueueWaitStats& writerFull, const QueueWaitStats& readerEmpty, int64_t t) {
        resetCounters();
        s_frame.frames = 0;
        s_frame.intervalStart = t;
        s_frame.writerFull = QueueSnapshot::take(writerFull);
        s_frame.readerEmpty = QueueSnapshot::take(readerEmpty);
        uint64_t cpu = 0;
        s_frame.cpuValid = threadCpu100ns(cpu);
        s_frame.cpu100ns = cpu;
        s_frame.cpuThreadId = GetCurrentThreadId();
      }

      void reportAsyncCreateFailures() {
        if (++s_frame.createReportFrames < kCreateFailureReportFrames) {
          return;
        }
        s_frame.createReportFrames = 0;
        const uint64_t total = s_asyncCreateFailures.load(std::memory_order_relaxed);
        if (total == s_frame.createFailuresReported) {
          return;
        }
        std::ostringstream ss;
        ss << "[Bridge] " << (total - s_frame.createFailuresReported) << " create call(s) failed on the server in the last "
           << kCreateFailureReportFrames << " frames (" << total << " in total). The game was told D3D_OK for each, and "
           << "buffer and surface uploads to those objects are skipped. Set sendCreateFunctionServerResponses = True "
           << "in bridge.conf to make the client wait for create results.";
        Logger::warn(ss.str());
        s_frame.createFailuresReported = total;
      }

      void logInterval(const QueueWaitStats& writerFull, const QueueWaitStats& readerEmpty, int64_t t) {
        const double perFrame = 1.0 / double(s_frame.frames);
        const QueueSnapshot full = QueueSnapshot::take(writerFull) - s_frame.writerFull;
        const QueueSnapshot empty = QueueSnapshot::take(readerEmpty) - s_frame.readerEmpty;

        uint64_t cpu = 0;
        const bool cpuValid = s_frame.cpuValid && threadCpu100ns(cpu) && GetCurrentThreadId() == s_frame.cpuThreadId &&
                              cpu >= s_frame.cpu100ns;
        const double cpuMsPerFrame = cpuValid ? double(cpu - s_frame.cpu100ns) / 1e4 * perFrame : -1.0;
        // The thread's CPU time includes spinning in the queue waits, which is not work.
        const double spinMsPerFrame = toUs(full.spinTicks + empty.spinTicks) / 1e3 * perFrame;

        uint64_t classCount[kClassCount] = {};
        std::vector<std::pair<uint32_t, uint32_t>> commands;
        std::vector<std::pair<uint64_t, uint32_t>> blocking;
        std::vector<std::pair<uint32_t, uint32_t>> dropped;
        uint64_t blockingCount = 0;
        uint64_t blockingTicks = 0;
        uint64_t droppedCount = 0;
        for (uint32_t i = 0; i < kMaxCommands; ++i) {
          const uint32_t droppedOfCommand = s_droppedCount[i].load(std::memory_order_relaxed);
          if (droppedOfCommand) {
            dropped.emplace_back(droppedOfCommand, i);
            droppedCount += droppedOfCommand;
          }
          const uint32_t count = s_cmdCount[i].load(std::memory_order_relaxed);
          if (count) {
            classCount[classify(i)] += count;
            commands.emplace_back(count, i);
          }
          const uint32_t waits = s_respWaitCount[i].load(std::memory_order_relaxed);
          if (waits) {
            const uint64_t waitTicks = s_respWaitTicks[i].load(std::memory_order_relaxed);
            blocking.emplace_back(waitTicks, i);
            blockingCount += waits;
            blockingTicks += waitTicks;
          }
        }
        std::sort(commands.begin(), commands.end(), std::greater<>());
        std::sort(blocking.begin(), blocking.end(), std::greater<>());
        std::sort(dropped.begin(), dropped.end(), std::greater<>());

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1);
        ss << "[BridgePerf] " << kSide << " frames=" << s_frame.frames
           << " frameMs=" << std::setprecision(2) << toUs(uint64_t(t - s_frame.intervalStart)) / 1e3 * perFrame;
        if (cpuValid) {
          ss << " threadCpuMsPerFrame=" << cpuMsPerFrame << " waitSpinMsPerFrame=" << spinMsPerFrame
             << " workCpuMsPerFrame=" << std::max(0.0, cpuMsPerFrame - spinMsPerFrame);
        }
        ss << std::setprecision(1)
           << " | cmdsPerFrame=" << double(s_commands.load(std::memory_order_relaxed)) * perFrame
           << " stateRecordsPerFrame=" << double(s_records.load(std::memory_order_relaxed)) * perFrame
           << " (by type, records included: ";
        for (uint32_t c = 0; c < kClassCount; ++c) {
          ss << (c ? " " : "") << kClassNames[c] << "=" << double(classCount[c]) * perFrame;
        }
        ss << ") dataKBPerFrame=" << double(s_dataWords.load(std::memory_order_relaxed)) * 4.0 / 1024.0 * perFrame
           << " uploadKBPerFrame=" << double(s_uploadBytes.load(std::memory_order_relaxed)) / 1024.0 * perFrame
#if defined(REMIX_BRIDGE_CLIENT)
           << " sendUsPerFrame="
#else
           << " processUsPerFrame="
#endif
           << toUs(s_cmdTicks.load(std::memory_order_relaxed)) * perFrame;
#if defined(REMIX_BRIDGE_CLIENT)
        ss << " | presentWaitUsPerFrame=" << toUs(s_presentWaitTicks.load(std::memory_order_relaxed)) * perFrame
           << " blockingPerFrame=" << double(blockingCount) * perFrame
           << " blockingUsPerFrame=" << toUs(blockingTicks) * perFrame
           << " | queueFull: waitsPerFrame=" << double(full.waits) * perFrame
           << " spinUsPerFrame=" << toUs(full.spinTicks) * perFrame
           << " blockedUsPerFrame=" << toUs(full.blockTicks) * perFrame
           << " | responseWait: spinUsPerFrame=" << toUs(empty.spinTicks) * perFrame
           << " blockedUsPerFrame=" << toUs(empty.blockTicks) * perFrame;
        {
          constexpr const char* kQueryPathNames[size_t(QueryPath::Count)] = { "clientEvent", "asyncCached", "asyncRequest", "blocking" };
          ss << " | queryGetDataPerFrame:";
          for (size_t i = 0; i < size_t(QueryPath::Count); ++i) {
            ss << " " << kQueryPathNames[i] << "=" << double(s_queryCalls[i].load(std::memory_order_relaxed)) * perFrame
               << "/" << double(s_querySignalled[i].load(std::memory_order_relaxed)) * perFrame;
          }
        }
#else
        ss << " | idle: waitsPerFrame=" << double(empty.waits) * perFrame
           << " spinUsPerFrame=" << toUs(empty.spinTicks) * perFrame
           << " blockedUsPerFrame=" << toUs(empty.blockTicks) * perFrame
           << " blocksPerFrame=" << double(empty.blocks) * perFrame
           << " | responseQueueFull: waitsPerFrame=" << double(full.waits) * perFrame
           << " spinUsPerFrame=" << toUs(full.spinTicks) * perFrame;
#endif
        ss << " | wakesSentPerFrame=" << double(full.wakesSent + empty.wakesSent) * perFrame;
        Logger::info(ss.str());

        std::ostringstream top;
        top << std::fixed << std::setprecision(1) << "[BridgePerf] " << kSide << " top commands per frame:";
        for (size_t i = 0; i < commands.size() && i < kTopCommands; ++i) {
          top << " " << shortName(commands[i].second) << "=" << double(commands[i].first) * perFrame;
        }
        Logger::info(top.str());

        if (!dropped.empty()) {
          std::ostringstream droppedLine;
          droppedLine << std::fixed << std::setprecision(1) << "[BridgePerf] " << kSide
                      << " redundant setters dropped per frame: total=" << double(droppedCount) * perFrame << " |";
          for (size_t i = 0; i < dropped.size() && i < kTopCommands; ++i) {
            droppedLine << " " << shortName(dropped[i].second) << "=" << double(dropped[i].first) * perFrame;
          }
          Logger::info(droppedLine.str());
        }

        if (!blocking.empty()) {
          std::ostringstream blockingLine;
          blockingLine << std::fixed << std::setprecision(1) << "[BridgePerf] " << kSide << " blocking calls per frame:";
          for (size_t i = 0; i < blocking.size() && i < kTopBlocking; ++i) {
            const uint32_t command = blocking[i].second;
            blockingLine << " " << shortName(command) << "=" << double(s_respWaitCount[command].load(std::memory_order_relaxed)) * perFrame
                         << " (" << toUs(blocking[i].first) * perFrame << "us)";
          }
          Logger::info(blockingLine.str());
        }
      }
    }

    void init(uint32_t intervalFrames) {
      g_intervalFrames.store(intervalFrames, std::memory_order_relaxed);
      if (intervalFrames) {
        Logger::info(format_string("[BridgePerf] %s counters on, logging every %u frames.", kSide, intervalFrames));
      }
    }

    int64_t now() {
      LARGE_INTEGER t;
      QueryPerformanceCounter(&t);
      return t.QuadPart;
    }

    void noteCommand(uint16_t command, uint32_t dataWords, int64_t ticks) {
      if (command < kMaxCommands) {
        s_cmdCount[command].fetch_add(1, std::memory_order_relaxed);
      }
      s_commands.fetch_add(1, std::memory_order_relaxed);
      s_dataWords.fetch_add(dataWords, std::memory_order_relaxed);
      s_cmdTicks.fetch_add(uint64_t(std::max<int64_t>(ticks, 0)), std::memory_order_relaxed);
    }

    void noteRecord(uint16_t command) {
      if (command < kMaxCommands) {
        s_cmdCount[command].fetch_add(1, std::memory_order_relaxed);
      }
      s_records.fetch_add(1, std::memory_order_relaxed);
    }

    void noteDropped(uint16_t command) {
      if (command < kMaxCommands) {
        s_droppedCount[command].fetch_add(1, std::memory_order_relaxed);
      }
    }

    void noteSent(uint16_t command) {
      s_lastSent.store(command, std::memory_order_relaxed);
    }

    void noteResponseWait(int64_t ticks) {
      const uint32_t command = s_lastSent.load(std::memory_order_relaxed);
      if (command < kMaxCommands) {
        s_respWaitCount[command].fetch_add(1, std::memory_order_relaxed);
        s_respWaitTicks[command].fetch_add(uint64_t(std::max<int64_t>(ticks, 0)), std::memory_order_relaxed);
      }
    }

    void notePresentWait(int64_t ticks) {
      s_presentWaits.fetch_add(1, std::memory_order_relaxed);
      s_presentWaitTicks.fetch_add(uint64_t(std::max<int64_t>(ticks, 0)), std::memory_order_relaxed);
    }

    void noteQuery(QueryPath path, bool signalled) {
      const size_t i = size_t(path);
      if (i < size_t(QueryPath::Count)) {
        s_queryCalls[i].fetch_add(1, std::memory_order_relaxed);
        if (signalled) {
          s_querySignalled[i].fetch_add(1, std::memory_order_relaxed);
        }
      }
    }

    void noteUpload(uint64_t bytes) {
      s_uploadBytes.fetch_add(bytes, std::memory_order_relaxed);
    }

    void noteAsyncCreateFailure(uint16_t command, long hresult) {
      const uint64_t n = s_asyncCreateFailures.fetch_add(1, std::memory_order_relaxed) + 1;
      if (n <= kCreateFailuresLoggedIndividually) {
        Logger::err(format_string("[Bridge] %s failed on the server with 0x%08x while the client did not wait for "
                                  "its result. The game was told D3D_OK. This is failure %u of the first %u that are "
                                  "logged individually.",
                                  Commands::toString(static_cast<Commands::D3D9Command>(command)).c_str(),
                                  static_cast<unsigned>(hresult), static_cast<unsigned>(n),
                                  static_cast<unsigned>(kCreateFailuresLoggedIndividually)));
      }
    }

    void onFrame(const QueueWaitStats& writerFull, const QueueWaitStats& readerEmpty) {
      reportAsyncCreateFailures();

      const uint32_t interval = g_intervalFrames.load(std::memory_order_relaxed);
      if (interval == 0) {
        s_frame.started = false;
        return;
      }
      const int64_t t = now();
      if (!s_frame.started) {
        s_frame.started = true;
        startInterval(writerFull, readerEmpty, t);
        return;
      }
      if (++s_frame.frames < interval) {
        return;
      }
      logInterval(writerFull, readerEmpty, t);
      startInterval(writerFull, readerEmpty, t);
    }
  }
}

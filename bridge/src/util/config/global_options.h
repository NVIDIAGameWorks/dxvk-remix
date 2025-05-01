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

#include "config/config.h"
#include "log/log.h"
#include "util_bridgecommand.h"

#include <d3d9.h>
#include <debugapi.h>
#include <vector>

class GlobalOptions {
  enum SharedHeapPolicy: uint32_t {
    Textures       = 1<<0,
    DynamicBuffers = 1<<1,
    StaticBuffers  = 1<<2,

    BuffersOnly    = DynamicBuffers | StaticBuffers,

    None           = 0,
    All            = Textures | DynamicBuffers | StaticBuffers,
  };

public:
  static void init() {
    get().initialize();
  }

  static uint32_t getModuleClientChannelMemSize() {
    return get().moduleClientChannelMemSize;
  }

  static uint32_t getModuleClientCmdQueueSize() {
    return get().moduleClientCmdQueueSize;
  }

  static uint32_t getModuleClientDataQueueSize() {
    return get().moduleClientDataQueueSize;
  }

  static uint32_t getModuleServerChannelMemSize() {
    return get().moduleServerChannelMemSize;
  }

  static uint32_t getModuleServerCmdQueueSize() {
    return get().moduleServerCmdQueueSize;
  }

  static uint32_t getModuleServerDataQueueSize() {
    return get().moduleServerDataQueueSize;
  }

  static uint32_t getClientChannelMemSize() {
    return get().clientChannelMemSize;
  }

  static uint32_t getClientCmdQueueSize() {
    return get().clientCmdQueueSize;
  }

  static uint32_t getClientDataQueueSize() {
    return get().clientDataQueueSize;
  }

  static uint32_t getServerChannelMemSize() {
    return get().serverChannelMemSize;
  }

  static uint32_t getServerCmdQueueSize() {
    return get().serverCmdQueueSize;
  }

  static uint32_t getServerDataQueueSize() {
    return get().serverDataQueueSize;
  }

  static bool getSendReadOnlyCalls() {
    return get().sendReadOnlyCalls;
  }

  static bool getSendAllServerResponses() {
    return get().sendAllServerResponses;
  }

  static bool getSendCreateFunctionServerResponses() {
    return get().sendCreateFunctionServerResponses;
  }

  static bool getLogAllCalls() {
    return get().logAllCalls;
  }

  static bool getLogApiCalls() {
    return get().logApiCalls;
  }

  static bool getLogAllCommands() {
    return get().logAllCommands;
  }

  static bool getLogServerCommands() {
    return get().logServerCommands || get().logAllCommands;
  }

  static uint32_t getCommandTimeout() {
#ifdef _DEBUG
    return (get().disableTimeouts || (IsDebuggerPresent() && get().disableTimeoutsWhenDebugging)) ? 0 : get().commandTimeout;
#else
    return get().disableTimeouts ? 0 : get().commandTimeout;
#endif
  }

  static uint32_t getStartupTimeout() {
#ifdef _DEBUG
    return (get().disableTimeouts || (IsDebuggerPresent() && get().disableTimeoutsWhenDebugging)) ? 0 : get().startupTimeout;
#else
    return get().disableTimeouts ? 0 : get().startupTimeout;
#endif
  }

  static uint32_t getAckTimeout() {
#ifdef _DEBUG
    return (get().disableTimeouts || (IsDebuggerPresent() && get().disableTimeoutsWhenDebugging)) ? 0 : get().ackTimeout;
#else
    return get().disableTimeouts ? 0 : get().ackTimeout;
#endif
  }

  static bool getDisableTimeouts() {
    return get().disableTimeouts;
  }
  static void setDisableTimeouts(bool disableTimeouts) {
    get().disableTimeouts = disableTimeouts;
  }

  static uint32_t getCommandRetries() {
    return get().infiniteRetries ? INFINITE : get().commandRetries;
  }

  static bool getInfiniteRetries() {
#ifdef REMIX_BRIDGE_SERVER
    return true;
#else 
    return get().infiniteRetries;
#endif
  }
  static void setInfiniteRetries(bool infiniteRetries) {
    get().infiniteRetries = infiniteRetries;
  }

  static bridge_util::LogLevel getLogLevel() {
    return get().logLevel;
  }

  static uint16_t getKeyStateCircBufMaxSize() {
    return get().keyStateCircBufMaxSize;
  }

  static uint8_t getPresentSemaphoreMaxFrames() {
    return get().presentSemaphoreMaxFrames;
  }

  static bool getPresentSemaphoreEnabled() {
    return get().presentSemaphoreEnabled;
  }

  static bool getCommandBatchingEnabled() {
    return get().commandBatchingEnabled;
  }

  static bool getUseSharedHeap() {
    return get().useSharedHeap;
  }

  static bool getUseSharedHeapForTextures() {
    return (get().sharedHeapPolicy & SharedHeapPolicy::Textures) != 0;
  }

  static bool getUseSharedHeapForDynamicBuffers() {
    return (get().sharedHeapPolicy & SharedHeapPolicy::DynamicBuffers) != 0;
  }

  static bool getUseSharedHeapForStaticBuffers() {
    return (get().sharedHeapPolicy & SharedHeapPolicy::StaticBuffers) != 0;
  }

  static const uint32_t getSharedHeapDefaultSegmentSize() {
    return get().sharedHeapDefaultSegmentSize;
  }

  static const uint32_t getSharedHeapChunkSize() {
    return get().sharedHeapChunkSize;
  }

  static const uint32_t getSharedHeapFreeChunkWaitTimeout() {
    return get().sharedHeapFreeChunkWaitTimeout;
  }

  static const uint32_t getSemaphoreTimeout() {
    return get().commandTimeout;
  }

  static uint32_t getThreadSafetyPolicy() {
    return get().threadSafetyPolicy;
  }

  static const uint32_t getServerSyncFlags() {
    uint32_t flags = 0;
    // The order of these flags needs to be read out in the same order
    // or else we will end up with wrong settings.
    flags |= getDisableTimeouts() ? 1 : 0;
    flags |= getInfiniteRetries() ? 2 : 0;
    return flags;
  }

  static void applyServerSyncFlags(uint32_t flags) {
    // Using same flag order from list above
    setDisableTimeouts((flags & 1) == 1);
    setInfiniteRetries((flags & 2) == 2);
    bridge_util::Logger::debug(bridge_util::format_string("Global settings are being applied from flags value %d", flags));
  }

  static const bool getAlwaysCopyEntireStaticBuffer() {
    return get().alwaysCopyEntireStaticBuffer;
  }
  

  static bool getExposeRemixApi() {
    return get().exposeRemixApi;
  }

  static bool getEliminateRedundantSetterCalls() {
    return get().eliminateRedundantSetterCalls;
  }

private:
  GlobalOptions() = default;

  void initialize() {
    // Default settings below
    // We only read the config values once from the config file and cache them in the
    // object so that it is transparent to the caller where the value is coming from.

    // Module Channel Defaults
    static constexpr size_t kDefaultModuleClientChannelMemSize = 4 << 20; // 4MB
    static constexpr size_t kDefaultModuleClientCmdQueueSize = 5;
    static constexpr size_t kDefaultModuleClientDataQueueSize = 25;
    static constexpr size_t kDefaultModuleServerChannelMemSize = 4 << 20; // 4MB
    static constexpr size_t kDefaultModuleServerCmdQueueSize = 5;
    static constexpr size_t kDefaultModuleServerDataQueueSize = 25;
    // Module Channel Options
    moduleClientChannelMemSize = bridge_util::Config::getOption<uint32_t>(
      "moduleClientChannelMemSize", kDefaultModuleClientChannelMemSize);
    moduleClientCmdQueueSize = bridge_util::Config::getOption<uint32_t>(
      "moduleClientCmdQueueSize", kDefaultModuleClientCmdQueueSize );
    moduleClientDataQueueSize = bridge_util::Config::getOption<uint32_t>(
      "moduleClientDataQueueSize", kDefaultModuleClientDataQueueSize);
    moduleServerChannelMemSize = bridge_util::Config::getOption<uint32_t>(
      "moduleServerChannelMemSize", kDefaultModuleServerChannelMemSize);
    moduleServerCmdQueueSize = bridge_util::Config::getOption<uint32_t>(
      "moduleServerCmdQueueSize", kDefaultModuleServerCmdQueueSize);
    moduleServerDataQueueSize = bridge_util::Config::getOption<uint32_t>(
      "moduleServerDataQueueSize", kDefaultModuleServerDataQueueSize);

    // Device Channel Defaults
    static constexpr size_t kDefaultClientChannelMemSize = 96 << 20; // 96MB
    static constexpr size_t kDefaultClientCmdQueueSize = 3 << 10; // 3k
    static constexpr size_t kDefaultClientDataQueueSize = 3 << 10; // 3k
    static constexpr size_t kDefaultServerChannelMemSize = 32 << 20; // 32MB
    static constexpr size_t kDefaultServerCmdQueueSize = 10;
    static constexpr size_t kDefaultServerDataQueueSize = 25;
    // Device Channel Options
    clientChannelMemSize = bridge_util::Config::getOption<uint32_t>(
      "clientChannelMemSize", kDefaultClientChannelMemSize);
    clientCmdQueueSize = bridge_util::Config::getOption<uint32_t>(
      "clientCmdQueueSize", kDefaultClientCmdQueueSize );
    clientDataQueueSize = bridge_util::Config::getOption<uint32_t>(
      "clientDataQueueSize", kDefaultClientDataQueueSize);
    serverChannelMemSize = bridge_util::Config::getOption<uint32_t>(
      "serverChannelMemSize", kDefaultServerChannelMemSize);
    serverCmdQueueSize = bridge_util::Config::getOption<uint32_t>(
      "serverCmdQueueSize", kDefaultServerCmdQueueSize);
    serverDataQueueSize = bridge_util::Config::getOption<uint32_t>(
      "serverDataQueueSize", kDefaultServerDataQueueSize);


    // Toggle this to also send read only calls to the server. This can be
    // useful for debugging to ensure the server side D3D is in the same state.
    sendReadOnlyCalls = bridge_util::Config::getOption<bool>("sendReadOnlyCalls", false);

    // Certain API calls from the client do not wait for a response from the server. Setting
    // sendAllServerResponses to true forces the server to respond and the clientside calls 
    // to wait for a response.
    sendAllServerResponses = bridge_util::Config::getOption<bool>("sendAllServerResponses", false);

    // Create API calls from the client wait for a response from the server by default,
    // but the wait can be disabled if both sendCreateFunctionServerResponses and
    // sendAllServerResponses are set to False.
    sendCreateFunctionServerResponses = bridge_util::Config::getOption<bool>("sendCreateFunctionServerResponses", true);

    // In a Debug or DebugOptimized build of the bridge, setting LogApiCalls
    // to True will write each call to a D3D9 API function through the bridge
    // client to the the client log file("bridge32.log").
    logApiCalls = bridge_util::Config::getOption<bool>("logApiCalls", false);

    // Like logApiCalls, setting LogAllCalls to True while running a
    // Debug or Debugoptimized build of the bridge will write each call
    // to a D3D9 API function through the bridge client to to the the
    // client log file("bridge32.log"), except both the entry and exit of
    // the call will be logged.This includes clientside internal calls to
    // D3D9API functions.Additionally, each nested internal call to a
    // public D3D9 API function will be offset by an additional tab.
    logAllCalls = bridge_util::Config::getOption<bool>("logAllCalls", false);

    // In a Debug or DebugOptimized build of the bridge, setting LogAllCommands
    // will log Command object creation, commands being pushed to the command buffer,
    // and waitForCommand calls to the respective Bridge server or client log files.
    // Additionally, it will enable logging of Bridge Server Module and Device
    // processing, the same as setting logServerCommands to True

    logAllCommands = bridge_util::Config::getOption<bool>("logAllCommands", false);

    // In a Debug or DebugOptimized build of the bridge, setting LogServerCommands
    // or LogAllCommands to True will write each command sent to the server to the server 
    // log file ("bridge64.log")

    logServerCommands = bridge_util::Config::getOption<bool>("logServerCommands", false);

    // These values strike a good balance between not waiting too long during the
    // handshake on startup, which we expect to be relatively quick, while still being
    // resilient enough against blips that can cause intermittent timeouts during
    // regular rendering due to texture loading or game blocking the render thread.
    commandTimeout = bridge_util::Config::getOption<uint32_t>("commandTimeout", 1'000);
    startupTimeout = bridge_util::Config::getOption<uint32_t>("startupTimeout", 100);
    commandRetries = bridge_util::Config::getOption<uint32_t>("commandRetries", 300);

    // The acknowledgement timeout is enforced at runtime on acknowledgement commands
    // like Ack and Continue to avoid hitting the long waits when an "unexpected"
    // command is picked up from the queue.
    ackTimeout = bridge_util::Config::getOption<uint32_t>("ackTimeout", 10);

    // If enabled sets the number of maximum retries for commands and semaphore wait
    // operations to INFINITE, therefore ensuring that even during long periods of
    // inactivity these calls won't time out.
    infiniteRetries = bridge_util::Config::getOption<bool>("infiniteRetries", false);
    
#if defined(_DEBUG) || defined(DEBUGOPT)
    constexpr char kDefaultLogLevel[] = "Debug";
#else
    constexpr char kDefaultLogLevel[] = "Info";
#endif
    const auto strLevel = bridge_util::Config::getOption<std::string>("logLevel", kDefaultLogLevel);
    logLevel = bridge_util::str_to_loglevel(strLevel);

    // We use a simple circular buffer to track user input state in order to send
    // it over the bridge for dxvk developer/user overlay manipulation. This sets
    // the max size of the circ buffer, which stores 2B elements. 100 is probably
    // overkill, but it's a fairly small cost.
    keyStateCircBufMaxSize = bridge_util::Config::getOption<uint16_t>("keyStateCircBufMaxSize", 100);

    // This is the maximum latency in number of frames the client can be ahead of the
    // server before it blocks and waits for the server to catch up. We want this value
    // to be rather small so the two processes don't get too far out of sync.
    presentSemaphoreMaxFrames = bridge_util::Config::getOption<uint8_t>("presentSemaphoreMaxFrames", 3);
    presentSemaphoreEnabled = bridge_util::Config::getOption<bool>("presentSemaphoreEnabled", true);

    // Toggles between waiting on and triggering the command queue semaphore for each
    // command separately when batching is off compared to waiting for it only once per
    // frame, used in conjunction with the Present semaphore above. Fewer semaphore
    // calls should give us better performance, so this is turned on by default.
    commandBatchingEnabled = bridge_util::Config::getOption<bool>("commandBatchingEnabled", false);

    // If this is enabled, timeouts will be set to their maximum value (INFINITE which is the max uint32_t) 
    // and retries will be set to 1 while the application is being launched with or attached to by a debugger
    disableTimeoutsWhenDebugging = bridge_util::Config::getOption<bool>("disableTimeoutsWhenDebugging", false);

    // Behaves the same as disableTimeoutsWhenDebugging, except that it does not require a debugger to be
    // attached. This is used to cover certain scenarios where an inactive game window may be running in
    // the background without actively rendering any frames for an undetermined amount of time.
    disableTimeouts = bridge_util::Config::getOption<bool>("disableTimeouts", true);

    // Rather than copying an entire index/vertex/etc. buffer on every buffer-type Unlock(), the bridge instead
    // directly stores all buffer data into a shared memory "heap" that both Client and Server are able to
    // access, providing a significant speed boost. Downside: Server/DXVK crashes are currently not recoverable.
    useSharedHeap = bridge_util::Config::getOption<bool>("useSharedHeap", false);

    initSharedHeapPolicy();

    // The SharedHeap is actually divvied up into multiple "segments":shared memory file mappings
    // This is that unit size
    static constexpr uint32_t kDefaultSharedHeapSegmentSize = 256 << 20; // 256MB
    sharedHeapDefaultSegmentSize = bridge_util::Config::getOption<uint32_t>("sharedHeapDefaultSegmentSize", kDefaultSharedHeapSegmentSize);

    // "shared heap chunk" size. Fundamental allocation unit size.
    static constexpr uint32_t kDefaultSharedHeapChunkSize = 4 << 10; // 4kB
    sharedHeapChunkSize = bridge_util::Config::getOption<uint32_t>("sharedHeapChunkSize", kDefaultSharedHeapChunkSize);

    // The number of seconds to wait for a avaliable chunk to free up in the shared heap
    sharedHeapFreeChunkWaitTimeout = bridge_util::Config::getOption<uint32_t>("sharedHeapFreeChunkWaitTimeout", 10);

    // Thread-safety policy: 0 - use client's choice, 1 - force thread-safe, 2 - force non-thread-safe
    threadSafetyPolicy = bridge_util::Config::getOption<uint32_t>("threadSafetyPolicy", 0);

    // If set and a buffer is not dynamic, vertex and index buffer lock/unlocks will ignore the bounds set during the lock call
    // and the brifge will copy the entire buffer. This means
    alwaysCopyEntireStaticBuffer = bridge_util::Config::getOption<bool>("alwaysCopyEntireStaticBuffer", false);
  
    exposeRemixApi = bridge_util::Config::getOption<bool>("exposeRemixApi", false);

    // If set, the bridge client will not send certain setter calls to the bridge server if the client knows the setter is writing
    // the the same value that is currently stored.
    eliminateRedundantSetterCalls = bridge_util::Config::getOption<bool>("eliminateRedundantSetterCalls", false);
  }

  void initSharedHeapPolicy();

  static GlobalOptions& get() {
    static GlobalOptions instance;
    return instance;
  }

  static GlobalOptions instance;

  uint32_t moduleClientChannelMemSize;
  uint32_t moduleClientCmdQueueSize;
  uint32_t moduleClientDataQueueSize;
  uint32_t moduleServerChannelMemSize;
  uint32_t moduleServerCmdQueueSize;
  uint32_t moduleServerDataQueueSize;
  uint32_t clientChannelMemSize;
  uint32_t clientCmdQueueSize;
  uint32_t clientDataQueueSize;
  uint32_t serverChannelMemSize;
  uint32_t serverCmdQueueSize;
  uint32_t serverDataQueueSize;
  bool sendReadOnlyCalls;
  bool sendAllServerResponses;
  bool sendCreateFunctionServerResponses;
  bool logAllCalls;
  bool logApiCalls;
  bool logAllCommands;
  bool logServerCommands;
  uint32_t commandTimeout;
  uint32_t startupTimeout;
  uint32_t ackTimeout;
  uint32_t commandRetries;
  bool infiniteRetries;
  bridge_util::LogLevel logLevel;
  uint16_t keyStateCircBufMaxSize;
  uint8_t presentSemaphoreMaxFrames;
  bool presentSemaphoreEnabled;
  bool commandBatchingEnabled;
  bool disableTimeoutsWhenDebugging;
  bool disableTimeouts;
  bool useSharedHeap;
  uint32_t sharedHeapPolicy;
  uint32_t sharedHeapSize;
  uint32_t sharedHeapDefaultSegmentSize;
  uint32_t sharedHeapChunkSize;
  uint32_t sharedHeapFreeChunkWaitTimeout;
  uint32_t threadSafetyPolicy;
  bool alwaysCopyEntireStaticBuffer;
  bool exposeRemixApi;
  bool eliminateRedundantSetterCalls;
};

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

#include "config/global_options.h"

#include "util_bridgecommand.h"
#include "util_singleton.h"

using DeviceBridge = Bridge<BridgeId::Device>;
using ClientMessage = DeviceBridge::Command;
using ServerMessage = DeviceBridge::Command;
static void initDeviceBridge() {
  DeviceBridge::init("Device",
#if defined(REMIX_BRIDGE_CLIENT)
                     GlobalOptions::getClientChannelMemSize(),
                     GlobalOptions::getClientCmdQueueSize(),
                     GlobalOptions::getClientDataQueueSize(),
                     GlobalOptions::getServerChannelMemSize(),
                     GlobalOptions::getServerCmdQueueSize(),
                     GlobalOptions::getServerDataQueueSize());
#elif defined(REMIX_BRIDGE_SERVER)
                     GlobalOptions::getServerChannelMemSize(),
                     GlobalOptions::getServerCmdQueueSize(),
                     GlobalOptions::getServerDataQueueSize(),
                     GlobalOptions::getClientChannelMemSize(),
                     GlobalOptions::getClientCmdQueueSize(),
                     GlobalOptions::getClientDataQueueSize());
#endif
}

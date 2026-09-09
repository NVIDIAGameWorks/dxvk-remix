/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#include <memory>
#include <mutex>

#include "../../util/rc/util_rc_ptr.h"
#include "../../util/util_singleton.h"

namespace dxvk {
  class RtxTextureManager;
  class thread;
  struct ManagedTexture;

  struct FileWatchTexturesImpl;


  class FileWatch : public Singleton<FileWatch> {
  public:
    explicit FileWatch();
    ~FileWatch();

    FileWatch(const FileWatch&) = delete;
    FileWatch(FileWatch&&) = delete;
    FileWatch& operator=(const FileWatch&) = delete;
    FileWatch& operator=(FileWatch&&) = delete;

    // Called by RtxTextureManager on device creation/destruction to enable/disable
    // texture hot-reload callbacks. The OS-level file watching runs independently.
    void setTextureManager(RtxTextureManager* textureManager);
    void clearTextureManager(RtxTextureManager* textureManager);

    void installDir(const char* dirpath);
    void removeAllWatchDirs();

    void watchTexture(RtxTextureManager* textureManager, const Rc<ManagedTexture>& tex);

    // Register a lightweight callback invoked on the FileWatch thread whenever any file
    // in a watched directory changes. Returns an id for removal - not a pointer, since a
    // freed object's address could be reused and collide with a still-queued request.
    using FileChangedFn = std::function<void(const std::filesystem::path&)>;
    using FileWatchCallbackId = uint64_t;
    FileWatchCallbackId addFileChangedCallback(FileChangedFn fn);
    void removeFileChangedCallback(FileWatchCallbackId id);

  private:
    void endThreadLocked();

    std::mutex m_mutex;
    std::unique_ptr<dxvk::thread> m_fileCheckingThread;
    std::unique_ptr<FileWatchTexturesImpl> m_impl;
  };

} // namespace dxvk

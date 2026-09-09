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

#include "rtx_file_watch.h"

#include "dxvk_device.h"
#include "rtx_texture.h"
#include "rtx_texture_manager.h"

#include <filesystem>

#include "rtx_options.h"


namespace {

  // returns a normalized path representation for safe comparisons
  inline std::filesystem::path makeCanonicalPath(const std::filesystem::path& rawPath) {
    // NOTE: using error_code to avoid exceptions
    std::error_code errorCodeCanonical{};
    auto canonicalPath = std::filesystem::canonical(rawPath, errorCodeCanonical);

    if (errorCodeCanonical != std::error_code{}) {
      dxvk::Logger::warn(dxvk::str::format(
        "Fail: std::filesystem::canonical (error='",
        errorCodeCanonical.message(),
        "'): '",
        rawPath.string(),
        "'"
      ));
      return {};
    }
    return canonicalPath;
  }


  inline std::filesystem::path makeCanonicalPathLexical(const std::filesystem::path& rawPath) {
    return rawPath.lexically_normal();
  }


  struct CanonicalPathHash {
    size_t operator()(const std::filesystem::path& value) const noexcept {
      // ensure that we compare / hash only the sanitized (canonical) paths
      assert(makeCanonicalPathLexical(value).native() == value.native());
      return std::hash<std::filesystem::path::string_type>{}(value.native());
    }
  };


  struct RcManagedTextureHash {
    std::size_t operator()(const dxvk::Rc<dxvk::ManagedTexture>& w) const noexcept {
      return std::hash<void*>{}(w.ptr());
    }
  };


  struct WatchFile {
    // a single file can correspond to N managed textures
    std::vector<dxvk::Rc<dxvk::ManagedTexture>> texturesReferencingFile{};
  };


  struct WatchDir {
    std::filesystem::path dirpath{};
    HANDLE dirHandle{};
    HANDLE watchEvent{};
    // files in this directory
    std::unordered_map<std::filesystem::path, WatchFile, CanonicalPathHash> files{};

    // stable pointers for async ReadDirectoryChanges calls, as WinAPI can populate them at any moment
    std::vector<uint8_t> nextChangesBuffer{};
    // status about IO request
    std::unique_ptr<OVERLAPPED> nextOverlapped{};
  };
} // namespace


namespace dxvk {
  struct RemoveAllRequest { };

  // Clears per-file texture registrations without closing directory handles.
  // Used when the TextureManager is replaced: old Rc<ManagedTexture> refs from
  // the previous session are released, and the new session registers fresh ones.
  struct ClearTextureRegistrationsRequest { };

  struct AddTextureRequest {
    Rc<ManagedTexture> tex;
  };

  struct AddCallbackRequest {
    FileWatch::FileWatchCallbackId id;
    FileWatch::FileChangedFn fn;
  };

  struct RemoveCallbackRequest {
    FileWatch::FileWatchCallbackId id;
  };

  struct FileWatchTexturesImpl {
    // NOTE: non-filewatch threads only push requests
    //       filewatch thread modifies all the members
    //       this way, only a single mutex for requests is needed
    std::vector<std::variant<std::filesystem::path, RemoveAllRequest, AddTextureRequest,
                             AddCallbackRequest, RemoveCallbackRequest,
                             ClearTextureRegistrationsRequest>> m_requests{};
    std::mutex m_requestsMutex{};

    // NOTE: 'm_dirs' and 'm_callbacks' are modified only by the filewatch thread
    std::vector<WatchDir> m_dirs{};
    std::unordered_map<FileWatch::FileWatchCallbackId, FileWatch::FileChangedFn> m_callbacks{};

    // Callback ids handed out by addFileChangedCallback; safe to generate from any thread.
    std::atomic<FileWatch::FileWatchCallbackId> m_nextCallbackId{ 1 };

    // Written by setTextureManager/clearTextureManager, read by the filewatch thread.
    std::atomic<dxvk::RtxTextureManager*> m_textureManager { nullptr };
    std::atomic_bool m_stop{};
  };
} // namespace dxvk


namespace {
  // 64 KiB is the practical max for ReadDirectoryChangesW, including over network drives.
  constexpr uint32_t READ_CHANGES_BUF_SIZE = 64 * 1024;


  BOOL readDirectoryChangesWrapper(
    HANDLE hDirectory,
    std::vector<uint8_t>& changesBuffer,
    const std::unique_ptr<OVERLAPPED>& overlapped // IO status
  ) {
    assert(changesBuffer.size() == READ_CHANGES_BUF_SIZE);
    return ReadDirectoryChangesW(
      hDirectory,
      changesBuffer.data(),
      changesBuffer.size(),
      TRUE, // check subtrees too
      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE
        | FILE_NOTIFY_CHANGE_CREATION,
      NULL,
      overlapped.get(),
      NULL
    );
  }


  // open a handle to the directory,
  // and create a new event to track changes that directory
  WatchDir openDir(const std::filesystem::path& dirpath) {
    HANDLE dirHandle = CreateFileW(
      dirpath.c_str(),
      FILE_LIST_DIRECTORY,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
      NULL
    );
    if (dirHandle == INVALID_HANDLE_VALUE) {
      dxvk::Logger::err(dxvk::str::format("Failed to open directory watch for: ", dirpath.string()));
      return {};
    }

    HANDLE watchEvent = CreateEvent(NULL, FALSE, 0, NULL);
    if (watchEvent == NULL) {
      CloseHandle(dirHandle);
      dxvk::Logger::err(dxvk::str::format("Failed to create watch event for: ", dirpath.string()));
      return {};
    }

    WatchDir dir{};
    {
      dir.dirpath = dirpath;
      dir.dirHandle = dirHandle;
      dir.watchEvent = watchEvent;
      dir.nextChangesBuffer.resize(READ_CHANGES_BUF_SIZE);
      dir.nextOverlapped = std::make_unique<OVERLAPPED>();
    }

    // start-up the first ReadDirectoryChanges call
    dir.nextOverlapped->hEvent = dir.watchEvent;
    BOOL resultReadDir = readDirectoryChangesWrapper(dir.dirHandle, dir.nextChangesBuffer, dir.nextOverlapped);
    if (!resultReadDir) {
      dxvk::Logger::
        err(dxvk::str::format("Initial ReadDirectoryChangesW failed (", int(resultReadDir), "): ", dirpath.string()));
    }

    return dir;
  }


  void filewatchInstallDir(dxvk::FileWatchTexturesImpl& watch, std::filesystem::path dirpath) {
    dirpath = makeCanonicalPath(dirpath);

    // NOTE: using error_code to avoid exceptions
    {
      std::error_code errorCodeExists{};
      if (!std::filesystem::exists(dirpath, errorCodeExists)) {
        dxvk::Logger::warn(
          dxvk::str::
            format("Fail: std::filesystem::exists (error='", errorCodeExists.message(), "'): '", dirpath.string(), "'")
        );
        return;
      }
    }
    {
      std::error_code errorCodeIsDirectory{};
      if (!std::filesystem::is_directory(dirpath, errorCodeIsDirectory)) {
        dxvk::Logger::warn(dxvk::str::format(
          "Fail: std::filesystem::is_directory (error='",
          errorCodeIsDirectory.message(),
          "'): '",
          dirpath.string(),
          "'"
        ));
        return;
      }
    }

    // add request, so the filewatch thread will open directory handle
    // as it handles the 'm_dirs' list
    {
      auto lockRequests = std::unique_lock{ watch.m_requestsMutex };
      watch.m_requests.push_back(dirpath);
    }
  }


  void filewatchAdd(dxvk::FileWatchTexturesImpl& watch, dxvk::Rc<dxvk::ManagedTexture> tex) {
    auto lockDirList = std::unique_lock{ watch.m_requestsMutex };
    watch.m_requests.push_back(dxvk::AddTextureRequest{ tex });
  }


  void filewatchUninstall(dxvk::FileWatchTexturesImpl& watch) {
    auto lockDirList = std::unique_lock{ watch.m_requestsMutex };
    watch.m_requests.push_back(dxvk::RemoveAllRequest{});
  }


  void filewatchCloseAllDirs(dxvk::FileWatchTexturesImpl& watch) {
    for (auto& dir : watch.m_dirs) {
      if (dir.dirHandle) {
        CloseHandle(dir.dirHandle);
        dir.dirHandle = NULL;
      }
      if (dir.watchEvent != NULL) {
        CloseHandle(dir.watchEvent);
        dir.watchEvent = NULL;
      }
    }
    watch.m_dirs.clear();
    dxvk::Logger::info("filewatch: uninstalled all directory watches");
  }


  bool directoryAlreadyInstalled(const dxvk::FileWatchTexturesImpl& watch, const std::filesystem::path& dirpath) {
    for (auto& dir : watch.m_dirs) {
      if (dir.dirpath.empty()) {
        assert(0 && "unexpected empty path on WatchDir");
        continue;
      }
      // NOTE: using error_code to avoid exceptions
      std::error_code ignoredErrorCode{};
      if (std::filesystem::equivalent(dirpath, dir.dirpath, ignoredErrorCode)) {
        return true;
      }
    }
    return false;
  }


  // find a directory that contains the file
  WatchDir* findParentWatchDir(dxvk::FileWatchTexturesImpl& watch, const std::filesystem::path& filepath) {
    for (WatchDir& potentialParentDir : watch.m_dirs) {
      auto texturePathRelativeToDirectory = filepath.lexically_relative(potentialParentDir.dirpath);
      // if lexically_relative failed
      if (texturePathRelativeToDirectory.empty()) {
        continue;
      }
      // if a texture path does NOT contain '..', then it's a child of the directory
      bool isTexturePathInSubtreeOfDir = texturePathRelativeToDirectory.native().find(L"..") == std::wstring::npos;
      if (!isTexturePathInSubtreeOfDir) {
        continue;
      }
      // found a WatchDir, subtree of which contains the file
      return &potentialParentDir;
    }

    dxvk::Logger::warn(
      dxvk::str::format("filewatch: can't add file: file is not in any of watched directories: ", filepath.string())
    );
    return nullptr;
  }


  void processRequests(
    dxvk::FileWatchTexturesImpl& watch,
    const std::unique_lock<std::mutex>& lockedRequests // pass for safety
  ) {
    if (watch.m_requests.empty()) {
      return;
    }
    for (const auto& req : watch.m_requests) {

      if (auto* dirpathToAdd = std::get_if<std::filesystem::path>(&req)) {
        // do not install the same directory
        if (!directoryAlreadyInstalled(watch, *dirpathToAdd)) {
          // open WinAPI handle
          WatchDir newDir = openDir(*dirpathToAdd);
          if (newDir.dirHandle && newDir.watchEvent) {
            watch.m_dirs.push_back(std::move(newDir));
            dxvk::Logger::info(dxvk::str::format("filewatch: installed directory watch for: ", dirpathToAdd->string()));
          }
        }
        continue;
      }

      if (auto* needToRemoveAllDirs = std::get_if<dxvk::RemoveAllRequest>(&req)) {
        filewatchCloseAllDirs(watch);
        continue;
      }

      if (auto* textureToAdd = std::get_if<dxvk::AddTextureRequest>(&req)) {
        const auto filepath = makeCanonicalPath(textureToAdd->tex->m_assetData->info().filename);
        if (!filepath.empty()) {
          if (WatchDir* parentDir = findParentWatchDir(watch, filepath)) {
            // a single texture file may be referenced by multiple ManagedTexture-s,
            // so keep a list: and if the file changes, reload all ManagedTexture-s that reference it
            // NOTE: creates a new entry if doesn't exists
            parentDir->files[filepath].texturesReferencingFile.push_back(textureToAdd->tex);
          }
        }
        continue;
      }

      if (std::get_if<dxvk::ClearTextureRegistrationsRequest>(&req)) {
        for (auto& dir : watch.m_dirs) {
          dir.files.clear();
        }
        continue;
      }

      if (auto* addCb = std::get_if<dxvk::AddCallbackRequest>(&req)) {
        watch.m_callbacks[addCb->id] = std::move(addCb->fn);
        continue;
      }

      if (auto* removeCb = std::get_if<dxvk::RemoveCallbackRequest>(&req)) {
        watch.m_callbacks.erase(removeCb->id);
        continue;
      }

      assert(0 && "unknown request");
    }
    watch.m_requests.clear();
  }


  // Notifies callbacks and reloads any managed textures referencing absFilepath. Shared
  // between the normal per-entry parse loop and the buffer-overflow recovery path below.
  void notifyFileChanged(dxvk::FileWatchTexturesImpl& watch, WatchDir& watchDir,
                          const std::filesystem::path& absFilepath) {
    for (const auto& [id, fn] : watch.m_callbacks) {
      fn(absFilepath);
    }

    auto f = watchDir.files.find(absFilepath);
    if (f == watchDir.files.end()) {
      return;
    }
    WatchFile& toreload = f->second;

    dxvk::Logger::info(dxvk::str::format(
      "filewatch: file changed, reloading ",
      toreload.texturesReferencingFile.size(),
      " managed textures: ",
      absFilepath.string()
    ));

    auto* texmanager = watch.m_textureManager.load(std::memory_order_acquire);
    if (texmanager != nullptr) {
      for (const auto& mat : toreload.texturesReferencingFile) {
        texmanager->requestHotReload(mat);
      }
    }
  }


  // A completion with zero bytes transferred signals a buffer overflow; contents are
  // then unspecified, so re-notify every file under the subtree instead of guessing.
  void notifyDirectoryOverflow(dxvk::FileWatchTexturesImpl& watch, WatchDir& watchDir) {
    dxvk::Logger::warn(dxvk::str::format(
      "filewatch: change notification buffer overflowed, rescanning: ", watchDir.dirpath.string()));

    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(
      watchDir.dirpath, std::filesystem::directory_options::skip_permission_denied, ec);
    const auto end = std::filesystem::recursive_directory_iterator();
    for (; !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file(ec)) {
        continue;
      }
      notifyFileChanged(watch, watchDir, makeCanonicalPathLexical(it->path()));
    }
  }


  void filewatchThreadFunc(dxvk::FileWatchTexturesImpl& watch) {
    dxvk::env::setThreadName("rtx-texture-filewatch");

    uint32_t currentdir = 0;

    while (!watch.m_stop.load()) {
      // Read live each iteration, so hotReloadRateMs changes at runtime take effect
      // without restarting the watcher thread.
      const DWORD WaitIntervalMS = std::clamp(dxvk::RtxOptions::TextureManager::hotReloadRateMs(), 10U, 10'000U);

      // loop through each directory watcher
      currentdir++;

      // NOTE: we can hold a pointer to 'm_dirs' item, since only this thread modifies 'm_dirs' list
      WatchDir* watchDir = nullptr;
      {
        auto lockRequests = std::unique_lock(watch.m_requestsMutex);

        processRequests(watch, lockRequests);

        if (!watch.m_dirs.empty()) {
          currentdir %= uint32_t(watch.m_dirs.size());
          watchDir = &watch.m_dirs[currentdir];
        }
      }

      if (!watchDir) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ WaitIntervalMS });
        continue;
      }

      std::vector<uint8_t> changesBuf{};
      {
        // block this thread by waiting for the request completion
        // NOTE: timeout is not 'INFINITE', as we may have > 1 directories to watch,
        //       so we cycle through them each 'WaitIntervalMS' interval
        if (WaitForSingleObject(watchDir->nextOverlapped->hEvent, WaitIntervalMS) != WAIT_OBJECT_0) {
          // we can still wait on the same ReadDirectoryChanges request
          continue;
        }

        BOOL resultGetOverlapped;
        DWORD bytesTransferred = 0;
        resultGetOverlapped = GetOverlappedResult(
          watchDir->dirHandle,
          watchDir->nextOverlapped.get(),
          &bytesTransferred,
          false // bWait is false, as we use WaitForSingleObject with a timeout
        );

        BOOL resultReadDir;
        {
          // 'changesBuf' now contains the directory changes,
          // 'nextChangesBuffer' is reinitialized, as it will be populated by the next ReadDirectoryChanges
          changesBuf = std::move(watchDir->nextChangesBuffer);
          watchDir->nextChangesBuffer.resize(READ_CHANGES_BUF_SIZE);

          // prepare OVERLAPPED structure for the next ReadDirectoryChanges call
          *watchDir->nextOverlapped = OVERLAPPED{};
          watchDir->nextOverlapped->hEvent = watchDir->watchEvent;

          // an async request to retrieve directory events (mainly file modifications)
          // we're scheduling next immediately after 'GetOverlappedResult' to not lose any events in-between
          resultReadDir = readDirectoryChangesWrapper(
            watchDir->dirHandle, //
            watchDir->nextChangesBuffer,
            watchDir->nextOverlapped
          );
        }

        if (!resultGetOverlapped) {
          dxvk::Logger::err(
            dxvk::str::format("GetOverlappedResult failed (", int(resultReadDir), "): ", watchDir->dirpath.string())
          );
          continue;
        }
        if (!resultReadDir) {
          dxvk::Logger::err(
            dxvk::str::format("ReadDirectoryChangesW failed (", int(resultReadDir), "): ", watchDir->dirpath.string())
          );
          continue;
        }
        if (bytesTransferred == 0) {
          notifyDirectoryOverflow(watch, *watchDir);
          continue;
        }
      }

      if (changesBuf.empty()) {
        assert(0 && "changes buffer was not reinitialized via ReadDirectoryChangesW");
        continue;
      }

      FILE_NOTIFY_INFORMATION* fileNotify = nullptr;

      while (true) // loop through entries in fileNotify
      {
        // get through notification entries
        {
          if (!fileNotify) { // if first entry
            fileNotify = (FILE_NOTIFY_INFORMATION*)changesBuf.data();
          } else {
            if (!fileNotify->NextEntryOffset) {
              break; // no more entries
            }
            fileNotify = (FILE_NOTIFY_INFORMATION*)(reinterpret_cast<uint8_t*>(fileNotify) + fileNotify->NextEntryOffset
            );
          }
        }

        // sanitize paths given by FILE_NOTIFY_INFORMATION
        auto filename = std::filesystem::path{
          std::wstring{ fileNotify->FileName, size_t(fileNotify->FileNameLength / sizeof(wchar_t)) }
        };
        if (filename.empty()) {
          continue;
        }
        auto absFilepath = makeCanonicalPathLexical(watchDir->dirpath / filename); // compose absolute path
        if (absFilepath.empty()) {
          continue;
        }

        notifyFileChanged(watch, *watchDir, absFilepath);
      }
    }
  }
} // namespace


dxvk::FileWatch::FileWatch() {
  m_impl = std::make_unique<FileWatchTexturesImpl>();
  m_fileCheckingThread = std::make_unique<dxvk::thread>([impl = m_impl.get()] {
    filewatchThreadFunc(*impl);
  });
  m_fileCheckingThread->set_priority(ThreadPriority::Lowest);
}


dxvk::FileWatch::~FileWatch() {
  auto lock = std::unique_lock{ m_mutex };
  endThreadLocked();
}


void dxvk::FileWatch::setTextureManager(RtxTextureManager* textureManager) {
  if (!m_impl) {
    return;
  }
  m_impl->m_textureManager.store(textureManager, std::memory_order_release);
}


void dxvk::FileWatch::clearTextureManager(RtxTextureManager* textureManager) {
  if (!m_impl) {
    return;
  }
  // Only clear if textureManager is still the active one; guards against a delayed
  // teardown from an old manager clearing a pointer set by a new one.
  RtxTextureManager* expected = textureManager;
  if (!m_impl->m_textureManager.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
    return;
  }
  // Clear per-file texture registrations so Rc<ManagedTexture> refs from this
  // session are released. Directories remain open so the new session can register
  // its own textures without re-installing dir handles.
  auto lockRequests = std::unique_lock{ m_impl->m_requestsMutex };
  m_impl->m_requests.push_back(ClearTextureRegistrationsRequest{});
}


void dxvk::FileWatch::endThreadLocked() {
  if (m_impl) {
    m_impl->m_stop = true;
  }

  if (m_fileCheckingThread) {
    m_fileCheckingThread->join();
    m_fileCheckingThread = {};
  }

  if (m_impl) {
    filewatchCloseAllDirs(*m_impl);
  }

  m_impl = {};
}


void dxvk::FileWatch::installDir(const char* dirpath) {
  if (!dirpath) {
    return;
  }

  auto lock = std::unique_lock{ m_mutex };
  if (!m_impl) {
    return;
  }
  filewatchInstallDir(*m_impl, dirpath);
}


void dxvk::FileWatch::removeAllWatchDirs() {
  auto lock = std::unique_lock{ m_mutex };
  if (!m_impl) {
    return;
  }
  filewatchUninstall(*m_impl);
}


void dxvk::FileWatch::watchTexture(RtxTextureManager* textureManager, const Rc<ManagedTexture>& tex) {
  if (!dxvk::RtxOptions::TextureManager::hotReload()) {
    return;
  }

  auto lock = std::unique_lock{ m_mutex };
  if (!m_impl || m_impl->m_textureManager.load(std::memory_order_acquire) != textureManager) {
    return;
  }
  if (!tex.ptr() || !tex->m_assetData.ptr() || !tex->m_assetData->info().filename) {
    return;
  }
  filewatchAdd(*m_impl, tex);
}


dxvk::FileWatch::FileWatchCallbackId dxvk::FileWatch::addFileChangedCallback(FileChangedFn fn) {
  auto lock = std::unique_lock{ m_mutex };
  if (!m_impl) {
    return 0;
  }
  const FileWatchCallbackId id = m_impl->m_nextCallbackId.fetch_add(1, std::memory_order_relaxed);
  auto lockRequests = std::unique_lock{ m_impl->m_requestsMutex };
  m_impl->m_requests.push_back(AddCallbackRequest{ id, std::move(fn) });
  return id;
}


void dxvk::FileWatch::removeFileChangedCallback(FileWatchCallbackId id) {
  auto lock = std::unique_lock{ m_mutex };
  if (!m_impl || id == 0) {
    return;
  }
  auto lockRequests = std::unique_lock{ m_impl->m_requestsMutex };
  m_impl->m_requests.push_back(RemoveCallbackRequest{ id });
}

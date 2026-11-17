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
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

namespace dxvk {

  // Tracks which pages of a slot table changed since the last upload.
  template<size_t SlotSize, size_t PageSize = 64 * 1024>
  struct UploadPages {
    static_assert(SlotSize > 0);
    static_assert(PageSize >= SlotSize);
    static_assert(PageSize % SlotSize == 0,
      "Upload page size must be divisible by slot size.");

    static constexpr size_t kSlotSize = SlotSize;
    static constexpr size_t kPageSize = PageSize;
    static constexpr size_t kSlotsPerPage = PageSize / SlotSize;

    explicit UploadPages(size_t maxSlotCount = 0)
      : m_maxSlotCount(maxSlotCount)
      , m_pages(maxSlotCount / kSlotsPerPage + size_t(maxSlotCount % kSlotsPerPage != 0), false) {
    }

    bool markSlot(size_t slotIndex) {
      if (slotIndex >= m_maxSlotCount) {
        assert(false);
        return false;
      }

      const size_t pageIndex = slotIndex / kSlotsPerPage;
      if (!m_pages[pageIndex]) {
        m_pages[pageIndex] = true;
        m_dirtyPageCount++;
      }
      return true;
    }

    bool empty() const {
      return m_dirtyPageCount == 0;
    }

    void clear() {
      if (m_dirtyPageCount != 0) {
        std::fill(m_pages.begin(), m_pages.end(), false);
        m_dirtyPageCount = 0;
      }
    }

    // Calls fn(byteOffset, byteSize) once per run of adjacent dirty pages.
    // Only the first usedSlotCount slots are covered, so the last range may end inside a page.
    template<typename Fn>
    void forEachRegion(size_t usedSlotCount, Fn&& fn) const {
      if (usedSlotCount > m_maxSlotCount) {
        assert(false);
        usedSlotCount = m_maxSlotCount;
      }
      if (m_dirtyPageCount == 0) {
        return;
      }

      const size_t usedByteCount = usedSlotCount * SlotSize;
      const size_t usedPageCount = usedSlotCount / kSlotsPerPage
        + size_t(usedSlotCount % kSlotsPerPage != 0);

      size_t pageIndex = 0;
      while (pageIndex < usedPageCount) {
        while (pageIndex < usedPageCount && !m_pages[pageIndex]) {
          pageIndex++;
        }
        if (pageIndex == usedPageCount) {
          break;
        }

        const size_t firstPage = pageIndex;
        while (++pageIndex < usedPageCount && m_pages[pageIndex]) {
        }

        const size_t byteOffset = firstPage * PageSize;
        const size_t byteEnd = std::min(pageIndex * PageSize, usedByteCount);
        fn(byteOffset, byteEnd - byteOffset);
      }
    }

  private:
    size_t m_maxSlotCount;
    std::vector<bool> m_pages;
    size_t m_dirtyPageCount = 0;
  };

}  // namespace dxvk

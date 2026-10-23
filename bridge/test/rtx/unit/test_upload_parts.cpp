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
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

#include "util_circularbuffer.h"
#include "util_uploadparts.h"

using namespace bridge_util;

namespace {
  void check(const bool condition, const std::string& message) {
    if (!condition) {
      throw message;
    }
  }

  // The parts and blobs of an upload cover every row once, in order, and stay within their caps.
  void testPartPlan() {
    const size_t rowSizes[] = { 65536, 16384, 8192, 300000, 3 << 20, 4 };
    const uint32_t rowCounts[] = { 4096, 1, 0, 7777 };
    for (const size_t rowSize : rowSizes) {
      for (const uint32_t numRows : rowCounts) {
        const uint32_t rowsPerPart = rowsPerChunk(rowSize, kMaxUploadPartBytes);
        const uint32_t rowsPerBlob = rowsPerChunk(rowSize, kMaxUploadBlobBytes);
        uint32_t covered = 0;
        uint32_t parts = 0;
        uint32_t firstRow = 0;
        do {
          const uint32_t partRows = std::min(rowsPerPart, numRows - firstRow);
          check(size_t(partRows) * rowSize <= std::max(kMaxUploadPartBytes, rowSize), "A part is larger than its cap.");
          for (uint32_t row = firstRow; row < firstRow + partRows;) {
            const uint32_t blobRows = std::min(rowsPerBlob, firstRow + partRows - row);
            check(blobRows > 0, "A blob holds no rows.");
            check(size_t(blobRows) * rowSize <= std::max(kMaxUploadBlobBytes, rowSize), "A blob is larger than its cap.");
            check(row == covered, "The rows are not covered in order.");
            covered += blobRows;
            row += blobRows;
          }
          firstRow += partRows;
          ++parts;
        } while (firstRow < numRows);
        check(covered == numRows, "The rows are not all covered.");
        check(parts >= 1, "A lock sends at least one part.");
      }
    }
  }

  // A command written to the data queue, as the reader sees it once its header is pushed.
  struct PushedPart {
    uint32_t firstRow;
    uint32_t partRows;
  };

  // Uploads a lock larger than the data queue part by part, with the overwrite rules of syncDataQueue: a write must
  // end at most one queue behind the position the reader published, and no further than one queue past the start of
  // its own command, because the reader reads none of a command before its header is pushed.
  void testUploadLargerThanQueue() {
    constexpr size_t kQueueWords = (size_t(24) << 20) / sizeof(uint32_t);
    constexpr size_t kRowSize = 65536;
    constexpr uint32_t kNumRows = 1024;  // 64 MB, more than twice the queue.
    std::vector<uint32_t> memory(kQueueWords);
    DataQueue writer("TestData", Accessor::Writer, memory.data(), kQueueWords * sizeof(uint32_t), 64);
    DataQueue reader("TestData", Accessor::Reader, memory.data(), kQueueWords * sizeof(uint32_t), 64);

    std::vector<uint8_t> source(kRowSize * kNumRows);
    for (size_t i = 0; i < source.size(); ++i) {
      source[i] = uint8_t((i * 2654435761u) >> 13);
    }
    std::vector<uint8_t> destination(source.size(), 0);

    std::deque<PushedPart> pushed;
    uint64_t readerPos = 0;
    size_t maxCommandWords = 0;

    const auto readPart = [&]() {
      const PushedPart part = pushed.front();
      pushed.pop_front();
      check(reader.pull() == part.firstRow, "The reader read another part's first row.");
      check(reader.pull() == part.partRows, "The reader read another part's row count.");
      for (uint32_t row = part.firstRow; row < part.firstRow + part.partRows;) {
        void* pBlob = nullptr;
        const size_t blobSize = reader.pull(&pBlob);
        check(pBlob != nullptr && blobSize % kRowSize == 0 && blobSize != 0, "The reader read a malformed blob.");
        const uint32_t blobRows = uint32_t(blobSize / kRowSize);
        memcpy(destination.data() + size_t(row) * kRowSize, pBlob, blobSize);
        row += blobRows;
      }
      readerPos = reader.get_abs_pos();
    };

    // Makes room for a write the way the client waits for the server.
    const auto sync = [&](const uint64_t batchStart, const size_t words, const bool sizedBlob) {
      const uint64_t writeEnd = writer.abs_pos_after(words, sizedBlob);
      check(writeEnd - batchStart <= kQueueWords, "A command does not fit in the data queue.");
      maxCommandWords = std::max<size_t>(maxCommandWords, size_t(writeEnd - batchStart));
      while (writeEnd > readerPos + kQueueWords) {
        check(!pushed.empty(), "The writer waits for space that no pushed command can free.");
        readPart();
      }
    };

    const uint32_t rowsPerPart = rowsPerChunk(kRowSize, kMaxUploadPartBytes);
    const uint32_t rowsPerBlob = rowsPerChunk(kRowSize, kMaxUploadBlobBytes);
    uint32_t firstRow = 0;
    do {
      const uint32_t partRows = std::min(rowsPerPart, kNumRows - firstRow);
      const uint64_t batchStart = writer.get_abs_pos();
      sync(batchStart, 2, false);
      writer.push_many(firstRow, partRows);
      for (uint32_t row = firstRow; row < firstRow + partRows;) {
        const uint32_t blobRows = std::min(rowsPerBlob, firstRow + partRows - row);
        const size_t blobBytes = size_t(blobRows) * kRowSize;
        sync(batchStart, blobBytes / sizeof(uint32_t) + 1, true);
        uint8_t* pBlob = nullptr;
        check(writer.begin_blob_push(blobBytes, pBlob) == Result::Success, "The writer could not begin a blob.");
        memcpy(pBlob, source.data() + size_t(row) * kRowSize, blobBytes);
        writer.end_blob_push();
        row += blobRows;
      }
      pushed.push_back({ firstRow, partRows });
      firstRow += partRows;
    } while (firstRow < kNumRows);

    while (!pushed.empty()) {
      readPart();
    }
    check(readerPos == writer.get_abs_pos(), "The reader and the writer disagree on the absolute position.");
    check(destination == source, "The rows arrived corrupted.");
    check(maxCommandWords * sizeof(uint32_t) <= kMaxUploadPartBytes + kMaxUploadBlobBytes + 256,
          "A part used more of the queue than its cap and one skipped tail.");

    // The same lock as one command needs more than the whole queue.
    check(size_t(kNumRows) * kRowSize > kQueueWords * sizeof(uint32_t), "The test lock must be larger than the queue.");
  }

  // abs_pos_after() predicts the position after a sized blob, including a blob moved to the start of the queue.
  void testAbsPosPrediction() {
    constexpr size_t kQueueWords = 1000;
    std::vector<uint32_t> memory(kQueueWords);
    DataQueue writer("TestData", Accessor::Writer, memory.data(), kQueueWords * sizeof(uint32_t), 4);
    std::vector<uint8_t> payload(kQueueWords * sizeof(uint32_t));
    const size_t blobWords[] = { 1, 10, 300, 997, 5, 998, 0, 600, 401, 3 };
    for (int lap = 0; lap < 5; ++lap) {
      for (const size_t words : blobWords) {
        const uint64_t predicted = writer.abs_pos_after(words + 1, true);
        writer.push(words * sizeof(uint32_t), payload.data());
        check(writer.get_abs_pos() == predicted, "abs_pos_after() mispredicted a sized blob of " +
                                                 std::to_string(words) + " words.");
        const uint64_t predictedWord = writer.abs_pos_after(1, false);
        writer.push(7u);
        check(writer.get_abs_pos() == predictedWord, "abs_pos_after() mispredicted a single word.");
      }
    }
  }
}

int main() {
  try {
    testPartPlan();
    testAbsPosPrediction();
    testUploadLargerThanQueue();
  } catch (const std::string& errorMessage) {
    std::cerr << "Upload parts test failed: " << errorMessage << std::endl;
    return -1;
  }
  std::cout << "Upload parts tests passed." << std::endl;
  return 0;
}

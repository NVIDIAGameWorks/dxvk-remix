#pragma once
#include <unordered_set>
#include "dxvk_memory.h"

namespace dxvk {

  // This struct uses RAII pattern to maintain information about GPU allocations.
  // Note, under RELEASE builds, we only store a fixed string allocation of 32 for 
  // the resource names, this unfortunately pollutes the code a little bit, in 
  // favour of minimal performance overhead in shipping games.
  struct GpuMemoryTracker {
    const static size_t MaxNameStringSize = 32;

    enum Type {
      Image = 0,
      Buffer,
      Unknown
    };

    GpuMemoryTracker() = default;
    GpuMemoryTracker(const char* name, Type type, dxvk::DxvkMemoryStats::Category category, VkExtent3D extents, VkFormat format);
    ~GpuMemoryTracker();

    static void onFrameEnd();
    static void renderGui();

    // Sometimes we dont know these things until the memory is allocated, so allow users to finalize this tracker.
    void finalize(size_t size, bool isDeviceResident, bool wasDemoted);

    // Sometimes a buffer can increase its size we need to allow that in tracking too.
    void updateSize(size_t size);

  private:

    struct Stats {
      // Make the name private since it has different definitions depending on build.  Use `getName`
#ifdef REMIX_SHIPPING
      char name[MaxNameStringSize] = "unnamed";
#else
      std::string name = "unnamed";
#endif

      Type type = Type::Unknown;
      size_t size = 0;
      dxvk::DxvkMemoryStats::Category category = dxvk::DxvkMemoryStats::Invalid;
      VkExtent3D extents = { 0, 0, 0 };
      VkFormat format = VK_FORMAT_UNDEFINED;
      bool isDeviceResident = false;
      bool wasDemoted = false;

      const char* getName() const;

      // Case insensitive string comparison supporting std::string and char[]
      int compareName(const Stats& other) const;

      void log() const;
    } m_stats;

    // Helper to copy the current state of all memory off to a list.
    static std::vector<Stats> copyToVector();

    // Track those allocs we release over the course of a frame when `includeWholeFrame` is enabled.
    inline static std::vector<GpuMemoryTracker::Stats> s_allocsReleasedInFrame;

    // Global list of allocs we are currently tracking
    inline static std::unordered_set<struct GpuMemoryTracker*> s_tracker;
    inline static mutex s_mutex;

  };
}
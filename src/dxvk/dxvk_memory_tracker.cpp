#pragma once
#include "dxvk_memory_tracker.h"
#include "rtx_render/rtx_imgui.h"
#include "rtx_render/rtx_option.h"
#include "imgui/imgui.h"

namespace dxvk {
  struct MemoryTrackerSettings {
    RTX_OPTION_FLAG("rtx.profiler.memory", bool, enable, false, RtxOptionFlags::NoSave, "Enables the memory profiler which allows users to inspect Remix resources using the profiler tool in the Dev Settings Remix window.  This option is disabled by default, and must be enabled from application launch to work correctly.");
    RTX_OPTION_FLAG("rtx.profiler.memory", bool, includeWholeFrame, false, RtxOptionFlags::NoSave, "Profiles memory across the entire frame when enabled.  When disabled we only see a snapshot of memory at the time of sampling.  This has some additional CPU performance overhead so is disabled by default.");
  };

  // If src is longer than (N-2), the last three characters of the copied
  // content are replaced with an ellipsis ("...") to indicate truncation.
  template <size_t N>
  static void copyFirstN(const char* src, char(&dst)[N]) {
    // We require at least 5 characters:
    //   N-2 (content) must be >= 3 so that "..." fits.
    static_assert(N >= 5, "Destination array must be at least 5 characters long.");

    // The content area is defined as the first (N-2) characters.
    constexpr std::size_t contentSize = N - 2;
    size_t srcLen = std::strlen(src);

    if (srcLen <= contentSize) {
      // No truncation: copy src into dest's content area.
      // (strncpy will pad with '\0' if src is shorter than content_size.)
      std::strncpy(dst, src, contentSize);
    } else {
      // Truncated: copy only the first (content_size - 3) characters,
      // then append "..." to indicate truncation.
      std::size_t numCopy = contentSize - 3;
      std::memcpy(dst, src, numCopy);
      dst[numCopy] = '.';
      dst[numCopy + 1] = '.';
      dst[numCopy + 2] = '.';
    }

    // Ensure that the last element is the null terminator.
    dst[N - 1] = '\0';
  }

  GpuMemoryTracker::GpuMemoryTracker(const char* name, Type type, DxvkMemoryStats::Category category, VkExtent3D extents, VkFormat format) {
    if (!MemoryTrackerSettings::enable()) {
      return;
    }

#ifdef REMIX_SHIPPING
    // In release builds, just copy the truncated string
    copyFirstN<MaxNameStringSize>(name, m_stats.name);
#else
    m_stats.name = name;
#endif
    m_stats.extents = extents;
    m_stats.format = format;
    m_stats.type = type;
    m_stats.category = category;

    std::lock_guard<mutex> lock(s_mutex);
    s_tracker.insert(this);
  }

  GpuMemoryTracker::~GpuMemoryTracker() {
    if (!MemoryTrackerSettings::enable()) {
      return;
    }
    std::lock_guard<mutex> lock(s_mutex);
    s_tracker.erase(this);
    if (MemoryTrackerSettings::includeWholeFrame()) {
      // Store off a copy of this tracker on release so we can count it against this frame
      s_allocsReleasedInFrame.emplace_back(std::move(m_stats));
    }
  }

  void GpuMemoryTracker::onFrameEnd() {
    std::lock_guard<mutex> lock(s_mutex);
    if (MemoryTrackerSettings::includeWholeFrame()) {
      // Clear this frame of allocations so we dont mistakenly leak trackers
      s_allocsReleasedInFrame.clear();
    }
  }


  void GpuMemoryTracker::finalize(size_t size, bool isDeviceResident, bool wasDemoted) {
    m_stats.size = size;
    m_stats.isDeviceResident = isDeviceResident;
    m_stats.wasDemoted = isDeviceResident && wasDemoted;
  }

  void GpuMemoryTracker::updateSize(size_t size) {
    m_stats.size = size;
  }

  const char* GpuMemoryTracker::Stats::getName() const {
#ifdef REMIX_SHIPPING
    return &name[0];
#else
    return name.c_str();
#endif
  }

  int GpuMemoryTracker::Stats::compareName(const Stats& other) const {
    const char* s1 = getName();
    const char* s2 = other.getName();

    while (*s1 && *s2) {
      int diff = std::tolower(static_cast<unsigned char>(*s1)) - std::tolower(static_cast<unsigned char>(*s2));
      if (diff != 0) {
        return diff;
      }
      ++s1;
      ++s2;
    }
    return std::tolower(static_cast<unsigned char>(*s1)) - std::tolower(static_cast<unsigned char>(*s2));
  }

  std::vector<GpuMemoryTracker::Stats> GpuMemoryTracker::copyToVector() {
    if (!MemoryTrackerSettings::enable()) {
      return std::vector<Stats>();
    }

    std::lock_guard<mutex> lock(s_mutex);

    // Entries to store
    size_t numEntries = s_tracker.size();
    if (MemoryTrackerSettings::includeWholeFrame()) {
      numEntries += s_allocsReleasedInFrame.size();
    }

    // Copy off the current state of memory into a vector so we can view it elsewhere
    uint32_t i = 0;
    std::vector<Stats> trackerAsVector(numEntries);
    for (const GpuMemoryTracker* pTracker : s_tracker) {
      trackerAsVector[i++] = pTracker->m_stats;
    }

    // Include the released allocations too if we have any
    if (MemoryTrackerSettings::includeWholeFrame()) {
      for (const GpuMemoryTracker::Stats& stats : s_allocsReleasedInFrame) {
        trackerAsVector[i++] = stats;
      }
    }

    return trackerAsVector;
  }

  std::string toString(GpuMemoryTracker::Type type) {
    switch (type) {
    case GpuMemoryTracker::Type::Image:   return "Image";
    case GpuMemoryTracker::Type::Buffer:  return "Buffer";
    case GpuMemoryTracker::Type::Unknown: return "Unknown";
    default:      return "Invalid";
    }
  }

  std::string toString(dxvk::DxvkMemoryStats::Category category) {
    switch (category) {
    case dxvk::DxvkMemoryStats::Invalid:                return "Invalid";
    case dxvk::DxvkMemoryStats::AppBuffer:              return "AppBuffer";
    case dxvk::DxvkMemoryStats::AppTexture:             return "AppTexture";
    case dxvk::DxvkMemoryStats::RTXReplacementGeometry: return "RTXReplacementGeometry";
    case dxvk::DxvkMemoryStats::RTXBuffer:              return "RTXBuffer";
    case dxvk::DxvkMemoryStats::RTXAccelerationStructure: return "RTXAccelerationStructure";
    case dxvk::DxvkMemoryStats::RTXOpacityMicromap:     return "RTXOpacityMicromap";
    case dxvk::DxvkMemoryStats::RTXMaterialTexture:     return "RTXMaterialTexture";
    case dxvk::DxvkMemoryStats::RTXRenderTarget:        return "RTXRenderTarget";
    default:                                          return "UnknownCategory";
    }
  }

  std::string toString(VkFormat format) {
    std::stringstream formatName;
    formatName << format;
    return formatName.str();
  }

  void GpuMemoryTracker::renderGui() {
    if (RemixGui::CollapsingHeader("Memory Profiler", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::TextWrapped("This is a tool to help diagnose memory related problems in Remix.  Press the `Sample Memory` button to take a snapshot of all the memory allocations currently in use by the application.  You can refresh this list at any time by clicking that button.  Once you have a snapshot, the data will be displayed as a list below.");

      if (!MemoryTrackerSettings::enable()) {
        ImGui::TextWrapped("Memory profiler is disabled.  Please enable in the rtx.conf with `rtx.profiler.memory.enable = True`.");
        return;
      }

  #ifdef REMIX_SHIPPING
      ImGui::TextWrapped("In release builds of Remix, we truncate the resource names to 32 characters for performance reasons.  Truncated names end with an ellipsis to signify this.");
  #endif
      static ImVector<int> selection;
      static std::vector<Stats> listOfAllocs;
      bool resort = false;
      if (ImGui::Button("Sample Memory")) {
        // Copy off the current state of memory into the GUI view of allocs
        listOfAllocs = GpuMemoryTracker::copyToVector();

        // Re-sort if necessary
        resort = true;

        // De-select all
        selection.clear();
      }
      ImGui::SameLine();

      RemixGui::Checkbox("Include Whole Frame", &MemoryTrackerSettings::includeWholeFrameObject());

      // Options
      static ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody
        | ImGuiTableFlags_ScrollY;

      enum GpuMemoryTrackerID {
        GpuMemoryTracker_Name = 0,
        GpuMemoryTracker_Type,
        GpuMemoryTracker_Size,
        GpuMemoryTracker_Category,
        GpuMemoryTracker_Extents,
        GpuMemoryTracker_Format,
        GpuMemoryTracker_isGpuResident,
        GpuMemoryTracker_wasDemoted,

        Count
      };

      if (ImGui::BeginTable("table_sorting", GpuMemoryTrackerID::Count, flags, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 15), 0.0f)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 0.0f, GpuMemoryTracker_Name);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 0.0f, GpuMemoryTracker_Type);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 0.0f, GpuMemoryTracker_Size);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 0.0f, GpuMemoryTracker_Category);
        ImGui::TableSetupColumn("Extents", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 0.0f, GpuMemoryTracker_Extents);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 0.0f, GpuMemoryTracker_Format);
        ImGui::TableSetupColumn("Is GPU?", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 0.0f, GpuMemoryTracker_isGpuResident);
        ImGui::TableSetupColumn("Was Demoted?", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 0.0f, GpuMemoryTracker_wasDemoted);
        ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
          if (sortSpecs->SpecsDirty || resort) {
            const auto sortComparison = [&sortSpecs](const Stats& a, const Stats& b) -> bool {
              for (int n = 0; n < sortSpecs->SpecsCount; n++) {
                const ImGuiTableColumnSortSpecs* sort_spec = &sortSpecs->Specs[n];
                int64_t delta = 0;
                switch (sort_spec->ColumnUserID) {
                case GpuMemoryTracker_Name:
                  delta = a.compareName(b);
                  break;
                case GpuMemoryTracker_Size:
                  delta = ((int64_t) a.size - (int64_t) b.size);
                  break;
                case GpuMemoryTracker_Category:
                  delta = ((int) a.category - (int) b.category);
                  break;
                case GpuMemoryTracker_Extents:
                  if (a.extents.width != b.extents.width) {
                    delta = (a.extents.width - b.extents.width);
                  } else if (a.extents.height != b.extents.height) {
                    delta = (a.extents.height - b.extents.height);
                  } else {
                    delta = (a.extents.depth - b.extents.depth);
                  }
                  break;
                case GpuMemoryTracker_Format:
                  delta = ((int) a.format - (int) b.format);
                  break;
                case GpuMemoryTracker_Type:
                  delta = ((int) a.type - (int) b.type);
                  break;
                case GpuMemoryTracker_isGpuResident:
                  delta = ((int) a.isDeviceResident - (int) b.isDeviceResident);
                  break;
                case GpuMemoryTracker_wasDemoted:
                  delta = ((int) a.wasDemoted - (int) b.wasDemoted);
                  break;
                default:
                  IM_ASSERT(0);
                  break;
                }
                if (delta != 0)
                  return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? (delta < 0) : (delta > 0);
              }
              return false;
            };

            std::sort(listOfAllocs.begin(), listOfAllocs.end(), sortComparison);

            sortSpecs->SpecsDirty = false;
            resort = false;
          }
        }

        ImGuiListClipper clipper;
        clipper.Begin(listOfAllocs.size());
        while (clipper.Step())
          for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; rowIdx++) {
            // Display a data item
            const Stats& item = listOfAllocs[rowIdx];
            char label[32];
            sprintf(label, "mem_prof_r_%04d", rowIdx);
            ImGui::PushID(label);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool isSelected = selection.contains(rowIdx);
            if (ImGui::Selectable(item.getName(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
              if (ImGui::GetIO().KeyCtrl) {
                if (isSelected) {
                  selection.find_erase_unsorted(rowIdx);
                } else {
                  selection.push_back(rowIdx);
                }
              } else if (ImGui::GetIO().KeyShift) {
                if (isSelected) {
                  selection.find_erase_unsorted(rowIdx);
                } else if (selection.size() > 0) {
                  int start = std::min(selection.back() + 1, rowIdx);
                  int end = std::max(selection.back() + 1, rowIdx);
                  for (int i = start; i <= end; i++) {
                    selection.push_back(i);
                  }
                } else {
                  selection.push_back(rowIdx);
                }
              } else {
                selection.clear();
                selection.push_back(rowIdx);
              }
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(toString(item.type).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(str::formatBytes(item.size).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(toString(item.category).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d x %d x %d", item.extents.width, item.extents.height, item.extents.depth);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(toString(item.format).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.isDeviceResident ? "Y" : "N");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.wasDemoted ? "Y" : "N");
            ImGui::PopID();
          }
        ImGui::EndTable();

        size_t totalVram = 0;
        size_t totalRam = 0;
        for (const int itemIdx : selection) {
          const Stats& item = listOfAllocs[itemIdx];
          if (item.isDeviceResident) {
            totalVram += item.size;
          } else {
            totalRam += item.size;
          }
        }

        ImGui::Separator();
        
        if (ImGui::Button("Write to Log")) {
          Logger::info("GPU Memory Profiler Log ---");
          // Write all allocs to the log file
          for (const auto& alloc : listOfAllocs) {
            alloc.log();
          }
        }

        ImGui::Separator();

        ImGui::Text(str::format("Selected: ", selection.size(), "\n"
                                "- Total VRAM: ", str::formatBytes(totalVram), "\n"
                                "- Total  RAM: ", str::formatBytes(totalRam), "\n"
        ).c_str());
      }
    }
  }

  void GpuMemoryTracker::Stats::log() const {
    Logger::info(str::format("Alloc:\n",
                 "  Name: ", getName(), "\n",
                 "  Type: ", toString(type), "\n",
                 "  Size: ", str::formatBytes(size), "\n",
                 "  Category: ", toString(category), "\n",
                 "  Extents: (", extents.width, " x ", extents.height, " x ", extents.depth, ")\n",
                 "  Format: ", toString(format), "\n",
                 "  DeviceResident: ", isDeviceResident ? "true" : "false", "\n",
                 "  WasDemoted: ", wasDemoted ? "true" : "false"));
  }
}
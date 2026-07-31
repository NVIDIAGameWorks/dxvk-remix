#include "../dxvk/rtx_render/rtx_types.h"
#include "../dxvk/rtx_render/rtx_option.h"
#include "../util/util_string.h"

#include <fstream>
#include <sstream>
#include <string>

namespace dxvk {
  // Single source of truth mapping each InstanceCategories value to its USD
  // attribute, schema display label, and (optionally) the RtxOption whose
  // description documents it. getInstanceCategorySubKey() and the exported
  // schema.usda are both derived from this table.
  struct RemixCategoryEntry {
    InstanceCategories category;
    const char* attr;        // USD attribute, e.g. "remix_category:world_ui"
    const char* displayName; // Schema UI label, e.g. "World UI"
    const char* optionName;  // RtxOption full name documenting this category, or nullptr
  };

  static constexpr RemixCategoryEntry kRemixCategoryEntries[] = {
    { InstanceCategories::WorldUI,                 "remix_category:world_ui",                 "World UI",                  "rtx.worldSpaceUiTextures" },
    { InstanceCategories::WorldMatte,              "remix_category:world_matte",              "World Matte",               "rtx.worldSpaceUiBackgroundTextures" },
    { InstanceCategories::Sky,                     "remix_category:sky",                      "Sky",                       "rtx.skyBoxTextures" },
    { InstanceCategories::Ignore,                  "remix_category:ignore",                   "Ignore",                    "rtx.ignoreTextures" },
    { InstanceCategories::IgnoreLights,            "remix_category:ignore_lights",            "Ignore Lights",             "rtx.ignoreLights" },
    { InstanceCategories::IgnoreAntiCulling,       "remix_category:ignore_anti_culling",      "Ignore Anti Culling",       "rtx.antiCulling.antiCullingTextures" },
    { InstanceCategories::IgnoreMotionBlur,        "remix_category:ignore_motion_blur",       "Ignore Motion Blur",        "rtx.postfx.motionBlurMaskOutTextures" },
    { InstanceCategories::IgnoreOpacityMicromap,   "remix_category:ignore_opacity_micromap",  "Ignore Opacity Micromap",   "rtx.opacityMicromapIgnoreTextures" },
    { InstanceCategories::IgnoreAlphaChannel,      "remix_category:ignore_alpha_channel",     "Ignore Alpha Channel",      "rtx.ignoreAlphaOnTextures" },
    { InstanceCategories::Hidden,                  "remix_category:hidden",                   "Hidden",                    "rtx.hideInstanceTextures" },
    { InstanceCategories::Particle,                "remix_category:particle",                 "Particle",                  "rtx.particleTextures" },
    { InstanceCategories::Beam,                    "remix_category:beam",                     "Beam",                      "rtx.beamTextures" },
    { InstanceCategories::DecalStatic,             "remix_category:decal_Static",             "Decal Static",              "rtx.decalTextures" },
    { InstanceCategories::DecalDynamic,            "remix_category:decal_dynamic",            "Decal Dynamic",             "rtx.dynamicDecalTextures" },
    { InstanceCategories::DecalSingleOffset,       "remix_category:decal_single_offset",      "Decal Single Offset",       "rtx.singleOffsetDecalTextures" },
    { InstanceCategories::DecalNoOffset,           "remix_category:decal_no_offset",          "Decal No Offset",           "rtx.nonOffsetDecalTextures" },
    { InstanceCategories::AlphaBlendToCutout,      "remix_category:alpha_blend_to_cutout",    "Alpha Blend To Cutout",     nullptr },
    { InstanceCategories::Terrain,                 "remix_category:terrain",                  "Terrain",                   "rtx.terrainTextures" },
    { InstanceCategories::AnimatedWater,           "remix_category:animated_water",           "Animated Water",            "rtx.animatedWaterTextures" },
    { InstanceCategories::ThirdPersonPlayerModel,  "remix_category:third_person_player_model","Third Person Player Model", "rtx.playerModelTextures" },
    { InstanceCategories::ThirdPersonPlayerBody,   "remix_category:third_person_player_body", "Third Person Player Body",  "rtx.playerModelBodyTextures" },
    { InstanceCategories::IgnoreBakedLighting,     "remix_category:ignore_baked_lighting",    "Ignore Baked Lighting",     "rtx.ignoreBakedLightingTextures" },
    { InstanceCategories::IgnoreTransparencyLayer, "remix_category:ignore_transparency_layer","Ignore Transparency Layer", "rtx.ignoreTransparencyLayerTextures" },
    { InstanceCategories::ParticleEmitter,         "remix_category:particle_emitter",         "Particle Emitter",          "rtx.particleEmitterTextures" },
    { InstanceCategories::SmoothNormals,           "remix_category:smooth_normals",           "Smooth Normals",            "rtx.smoothNormalsTextures" },
    { InstanceCategories::HairCards,               "remix_category:hair_cards",               "Hair Cards",                "rtx.hairCardTextures" },
  };

  // Table must have one entry per enum value.
  static_assert(sizeof(kRemixCategoryEntries) / sizeof(kRemixCategoryEntries[0]) == (size_t) InstanceCategories::Count,
                "Please add/remove the category to kRemixCategoryEntries above.");

  // Table must be in enum order so getInstanceCategorySubKey() can index it directly.
  constexpr bool remixCategoryEntriesMatchEnumOrder() {
    for (uint32_t i = 0; i < (uint32_t) InstanceCategories::Count; ++i) {
      if ((uint32_t) kRemixCategoryEntries[i].category != i) {
        return false;
      }
    }
    return true;
  }
  static_assert(remixCategoryEntriesMatchEnumOrder(),
                "kRemixCategoryEntries must be listed in the same order as the InstanceCategories enum.");

  // Used when reading/writing with Remix USD mods.
  static const char* getInstanceCategorySubKey(InstanceCategories cat) {
    if (cat >= InstanceCategories::Count) {
      return "";
    }
    return kRemixCategoryEntries[(uint32_t) cat].attr;
  }

  // The category's RtxOption description if it has one, else a generic fallback.
  inline std::string getRemixCategoryDoc(const RemixCategoryEntry& entry) {
    if (entry.optionName != nullptr) {
      const RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName(entry.optionName);
      if (option != nullptr && option->getDescription() != nullptr && option->getDescription()[0] != '\0') {
        return option->getDescription();
      }
    }
    return std::string("Remix instance category: ") + entry.displayName + ".";
  }

  // Render the RemixCategories schema.usda from kRemixCategoryEntries. This
  // singleApply API schema is compiled by usdGenSchema into the RemixCategories
  // USD plugin; test_remix_categories keeps the checked-in copy in sync.
  inline std::string getRemixCategoriesSchemaUsda() {
    std::stringstream ss;
    ss << "#usda 1.0\n";
    ss << "(\n";
    ss << "    \"\"\"GENERATED - Remix instance categories exposed as boolean USD attributes. Do not edit by hand; edit kRemixCategoryEntries in src/lssusd/usd_common.h and regenerate via the test_remix_categories unit test.\"\"\"\n";
    ss << "    subLayers = [\n";
    ss << "        @usd/schema.usda@\n";
    ss << "    ]\n";
    ss << ")\n";
    ss << "\n";
    ss << "over \"GLOBAL\" (\n";
    ss << "    customData = {\n";
    ss << "        string libraryName = \"RemixCategories\"\n";
    ss << "        string libraryPath = \".\"\n";
    ss << "    }\n";
    ss << ")\n";
    ss << "{\n";
    ss << "}\n";
    ss << "\n";
    ss << "class \"RemixInstanceCategoryAPI\" (\n";
    ss << "    inherits = </APISchemaBase>\n";
    ss << "    customData = {\n";
    ss << "        token apiSchemaType = \"singleApply\"\n";
    ss << "    }\n";
    ss << "    doc = \"\"\"Adds Remix instance category flags to a prim. Each boolean marks the instance with the corresponding Remix draw-call category.\"\"\"\n";
    ss << ")\n";
    ss << "{\n";
    for (const RemixCategoryEntry& entry : kRemixCategoryEntries) {
      ss << "    bool " << entry.attr << " = 0 (\n";
      ss << "        doc = \"" << str::escapeCStyle(getRemixCategoryDoc(entry)) << "\"\n";
      ss << "        displayGroup = \"Remix Categories\"\n";
      ss << "        displayName = \"" << entry.displayName << "\"\n";
      ss << "    )\n";
    }
    ss << "}\n";
    return ss.str();
  }

  // Write schema.usda to the given path.
  inline bool writeRemixCategoriesSchemaUsda(const char* outputFilePath) {
    std::ofstream file(outputFilePath);
    if (!file.is_open()) {
      Logger::err(std::string("Failed to open remix categories schema output file: ") + outputFilePath);
      return false;
    }
    file << getRemixCategoriesSchemaUsda();
    return true;
  }
}

// Exported for test_remix_categories.cpp to regenerate and diff schema.usda.
#ifdef _WIN32
extern "C" __declspec(dllexport) bool writeRemixCategoriesSchemaUsda(const char* outputFilePath);
#else
extern "C" __attribute__((visibility("default"))) bool writeRemixCategoriesSchemaUsda(const char* outputFilePath);
#endif

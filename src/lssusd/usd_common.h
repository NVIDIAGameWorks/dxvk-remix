#include "../dxvk/rtx_render/rtx_types.h"

namespace dxvk {
  // Used when readin/writing with Remix USD mods.
  static const char* getInstanceCategorySubKey(InstanceCategories cat) {
    static_assert((uint32_t) InstanceCategories::Count == 24, "Please add/remove the category to the below table.");
    switch (cat) {
    case InstanceCategories::WorldUI:
      return "remix_category:world_ui";
    case InstanceCategories::WorldMatte:
      return "remix_category:world_matte";
    case InstanceCategories::Sky:
      return "remix_category:sky";
    case InstanceCategories::Ignore:
      return "remix_category:ignore";
    case InstanceCategories::IgnoreLights:
      return "remix_category:ignore_lights";
    case InstanceCategories::IgnoreAntiCulling:
      return "remix_category:ignore_anti_culling";
    case InstanceCategories::IgnoreMotionBlur:
      return "remix_category:ignore_motion_blur";
    case InstanceCategories::IgnoreOpacityMicromap:
      return "remix_category:ignore_opacity_micromap";
    case InstanceCategories::IgnoreAlphaChannel:
      return "remix_category:ignore_alpha_channel";
    case InstanceCategories::Hidden:
      return "remix_category:hidden";
    case InstanceCategories::Particle:
      return "remix_category:particle";
    case InstanceCategories::Beam:
      return "remix_category:beam";
    case InstanceCategories::DecalStatic:
      return "remix_category:decal_Static";
    case InstanceCategories::DecalDynamic:
      return "remix_category:decal_dynamic";
    case InstanceCategories::DecalSingleOffset:
      return "remix_category:decal_single_offset";
    case InstanceCategories::DecalNoOffset:
      return "remix_category:decal_no_offset";
    case InstanceCategories::AlphaBlendToCutout:
      return "remix_category:alpha_blend_to_cutout";
    case InstanceCategories::Terrain:
      return "remix_category:terrain";
    case InstanceCategories::AnimatedWater:
      return "remix_category:animated_water";
    case InstanceCategories::ThirdPersonPlayerModel:
      return "remix_category:third_person_player_model";
    case InstanceCategories::ThirdPersonPlayerBody:
      return "remix_category:third_person_player_body";
    case InstanceCategories::IgnoreBakedLighting:
      return "remix_category:ignore_baked_lighting";
    case InstanceCategories::IgnoreTransparencyLayer:
      return "remix_category:ignore_transparency_layer";
    case InstanceCategories::ParticleEmitter:
      return "remix_category:particle_emitter";
    default:
      Logger::err("Category key name requested, but no category found.");
      return "";
    }
  }
}
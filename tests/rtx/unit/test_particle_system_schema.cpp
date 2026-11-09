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

#include <filesystem>
#include <iostream>
#include <set>
#include <string>

#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/base/plug/registry.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usd/stage.h>
#include "../../../src/lssusd/usd_include_end.h"
#include "../../../src/lssusd/particle_system_helpers.h"


namespace {
  std::set<std::string> getExpectedProperties() {
    std::set<std::string> properties = {
      "primvars:particle:minSpawnColor",
      "primvars:particle:maxSpawnColor",
      "primvars:particle:minTargetColor",
      "primvars:particle:maxTargetColor",
      "primvars:particle:minSpawnSize",
      "primvars:particle:maxSpawnSize",
      "primvars:particle:minTargetSize",
      "primvars:particle:maxTargetSize",
      "primvars:particle:minSpawnRotationSpeed",
      "primvars:particle:maxSpawnRotationSpeed",
      "primvars:particle:minTargetRotationSpeed",
      "primvars:particle:maxTargetRotationSpeed",
      "primvars:particle:attractorPosition",
      "primvars:particle:attractorForce",
      "primvars:particle:minTimeToLive",
      "primvars:particle:maxTimeToLive",
      "primvars:particle:initialVelocityFromNormal",
      "primvars:particle:initialVelocityConeAngleDegrees",
      "primvars:particle:turbulenceFrequency",
      "primvars:particle:turbulenceForce",
      "primvars:particle:motionTrailMultiplier",
      "primvars:particle:spawnRatePerSecond",
      "primvars:particle:spawnDistribution",
      "primvars:particle:minScreenSize",
      "primvars:particle:maxScreenSize",
      "primvars:particle:screenSizeCoverage",
      "primvars:particle:collisionThickness",
      "primvars:particle:collisionRestitution",
      "primvars:particle:initialRotationDeviationDegrees",
      "primvars:particle:spawnBurstDuration",
      "primvars:particle:dragCoefficient",
      "primvars:particle:attractorRadius",
      "primvars:particle:gravityForce",
      "primvars:particle:initialVelocityFromMotion",
      "primvars:particle:maxNumParticles",
      "primvars:particle:billboardType",
      "primvars:particle:spriteSheetMode",
      "primvars:particle:collisionMode",
      "primvars:particle:randomFlipAxis",
      "primvars:particle:hideEmitter",
      "primvars:particle:enableMotionTrail",
      "primvars:particle:useTurbulence",
      "primvars:particle:alignParticlesToVelocity",
      "primvars:particle:useSpawnTexcoords",
      "primvars:particle:enableCollisionDetection",
      "primvars:particle:restrictVelocityX",
      "primvars:particle:restrictVelocityY",
      "primvars:particle:restrictVelocityZ",
    };

    for (const char* channel : { "minColor", "maxColor" }) {
      for (const char* suffix : { "times", "values" }) {
        properties.emplace(std::string("primvars:particle:") + channel + ":" + suffix);
      }
    }

    for (const char* channel : {
      "minSize:x", "minSize:y", "maxSize:x", "maxSize:y",
      "minRotationSpeed", "maxRotationSpeed",
      "maxVelocity:x", "maxVelocity:y", "maxVelocity:z" }) {
      for (const char* suffix : {
        "times", "values", "inTangentTypes", "outTangentTypes",
        "inTangentValues", "outTangentValues", "inTangentTimes",
        "outTangentTimes", "tangentBrokens" }) {
        properties.emplace(std::string("primvars:particle:") + channel + ":" + suffix);
      }
    }

    return properties;
  }

  bool testScreenSizeSchemaAndAuthoredValues(const pxr::UsdPrimDefinition& definition, const pxr::SdfLayerRefPtr& schemaLayer) {
    struct ExpectedProperty {
      const char* name;
      const char* displayName;
      float fallback;
      float authored;
      float maximum;
    };
    const ExpectedProperty expected[] = {
      { "minScreenSize", "Min Screen Size (Pixels)", 0.f, 0.8f, 100000.f },
      { "maxScreenSize", "Max Screen Size (Pixels)", 0.f, 1.2f, 100000.f },
      { "screenSizeCoverage", "Coverage Compensation", 1.f, 0.25f, 1.f },
    };

    for (const auto& property : expected) {
      const pxr::TfToken name(std::string("primvars:particle:") + property.name);
      float fallback = -1.f;
      std::string group;
      std::string displayName;
      const bool hasFallback = definition.GetAttributeFallbackValue(name, &fallback);
      const bool hasGroup = definition.GetPropertyMetadata(name, pxr::TfToken("displayGroup"), &group);
      const bool hasDisplayName = definition.GetPropertyMetadata(name, pxr::TfToken("displayName"), &displayName);
      if (!hasFallback || fallback != property.fallback || !hasGroup || group != "Visual"
          || !hasDisplayName || displayName != property.displayName) {
        std::cerr << "Screen-size fallback or Properties metadata mismatch: " << name
          << ", fallback=" << hasFallback << ":" << fallback << ", group=" << hasGroup << ":" << group
          << ", displayName=" << hasDisplayName << ":" << displayName << "\n";
        return false;
      }
      // USD excludes customData from schema fallback metadata, so inspect its authored spec.
      const auto spec = schemaLayer->GetAttributeAtPath(pxr::SdfPath("/ParticleSystemAPI").AppendProperty(name));
      const auto metadata = spec ? spec->GetInfo(pxr::TfToken("customData")) : pxr::VtValue();
      if (!metadata.IsHolding<pxr::VtDictionary>()) {
        std::cerr << "Screen-size authored metadata is missing: " << name << "\n";
        return false;
      }
      const auto& customData = metadata.UncheckedGet<pxr::VtDictionary>();
      const auto limits = customData.find("limits");
      if (limits == customData.end() || !limits->second.IsHolding<pxr::VtDictionary>()) {
        std::cerr << "Screen-size Properties limits are missing: " << name << "\n";
        return false;
      }
      const auto& limitData = limits->second.UncheckedGet<pxr::VtDictionary>();
      const auto hard = limitData.find("hard");
      if (hard == limitData.end() || !hard->second.IsHolding<pxr::VtDictionary>()) {
        std::cerr << "Screen-size hard limits are missing: " << name << "\n";
        return false;
      }
      const auto& hardData = hard->second.UncheckedGet<pxr::VtDictionary>();
      const auto minimum = hardData.find("minimum");
      const auto maximum = hardData.find("maximum");
      if (minimum == hardData.end() || maximum == hardData.end()
          || !minimum->second.IsHolding<float>() || minimum->second.UncheckedGet<float>() != 0.f
          || !maximum->second.IsHolding<float>() || maximum->second.UncheckedGet<float>() != property.maximum) {
        std::cerr << "Screen-size Properties bounds mismatch: " << name << "\n";
        return false;
      }
    }

    const auto stage = pxr::UsdStage::CreateInMemory();
    for (const char* type : { "Mesh", "Material" }) {
      const pxr::SdfPath path(std::string("/") + type + "Emitter");
      const auto prim = stage->DefinePrim(path, pxr::TfToken(type));
      prim.AddAppliedSchema(pxr::TfToken("ParticleSystemAPI"));
      for (const auto& property : expected) {
        const pxr::TfToken name(std::string("primvars:particle:") + property.name);
        const auto attribute = prim.GetAttribute(name);
        pxr::VtValue value;
        float converted = -1.f;
        if (attribute.GetTypeName() != pxr::SdfValueTypeNames->Float || !attribute.Get(&value)
            || !lss::ConvertPrimvarValue(value, converted) || converted != property.fallback) {
          std::cerr << "Screen-size defaults must load for " << type << " emitters: " << name << "\n";
          return false;
        }
        if (!attribute.Set(property.authored) || !attribute.Get(&value)
            || !lss::ConvertPrimvarValue(value, converted) || converted != property.authored) {
          std::cerr << "Authored screen size must round-trip for " << type << " emitters: " << name << "\n";
          return false;
        }
      }
    }

    std::string serialized;
    const auto layer = pxr::SdfLayer::CreateAnonymous();
    if (!stage->GetRootLayer()->ExportToString(&serialized) || !layer->ImportFromString(serialized)) {
      std::cerr << "Screen-size authoring must serialize successfully.\n";
      return false;
    }
    const auto reopened = pxr::UsdStage::Open(layer);
    if (!reopened) {
      std::cerr << "Screen-size authoring must reopen successfully.\n";
      return false;
    }
    for (const char* type : { "Mesh", "Material" }) {
      const auto prim = reopened->GetPrimAtPath(pxr::SdfPath(std::string("/") + type + "Emitter"));
      for (const auto& property : expected) {
        pxr::VtValue value;
        float converted = -1.f;
        if (!prim.GetAttribute(pxr::TfToken(std::string("primvars:particle:") + property.name)).Get(&value)
            || !lss::ConvertPrimvarValue(value, converted) || converted != property.authored) {
          std::cerr << "Screen-size values changed across save/reload for " << type << ".\n";
          return false;
        }
      }
    }
    float invalidValueDestination = 0.75f;
    if (lss::ConvertPrimvarValue(pxr::VtValue(pxr::TfToken("invalid")), invalidValueDestination)
        || invalidValueDestination != 0.75f) {
      std::cerr << "A nonnumeric screen-size value must not overwrite the existing value.\n";
      return false;
    }
    return true;
  }
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Expected the RemixParticleSystem plugin DLL path.\n";
    return -1;
  }

  const std::filesystem::path pluginDir = std::filesystem::path(argv[1]).parent_path() / "resources";
  pxr::PlugRegistry::GetInstance().RegisterPlugins(pluginDir.string());

  const pxr::UsdPrimDefinition* primDef = pxr::UsdSchemaRegistry::GetInstance()
    .FindAppliedAPIPrimDefinition(pxr::TfToken("ParticleSystemAPI"));
  if (primDef == nullptr) {
    std::cerr << "ParticleSystemAPI prim definition was not registered from " << pluginDir << ".\n";
    return -1;
  }

  const std::set<std::string> expected = getExpectedProperties();
  std::set<std::string> actual;
  for (const pxr::TfToken& property : primDef->GetPropertyNames()) {
    actual.emplace(property.GetString());
  }

  if (actual.size() != lss::kParticleSystemSchemaPropertyCount) {
    std::cerr << "Particle schema property count disagrees with the USD reader.\n";
    return -1;
  }

  if (actual != expected) {
    for (const std::string& property : expected) {
      if (actual.find(property) == actual.end()) {
        std::cerr << "Missing schema property: " << property << "\n";
      }
    }
    for (const std::string& property : actual) {
      if (expected.find(property) == expected.end()) {
        std::cerr << "Unexpected schema property: " << property << "\n";
      }
    }
    return -1;
  }

  const pxr::TfToken distributionProperty("primvars:particle:spawnDistribution");
  pxr::TfToken distributionDefault;
  if (!primDef->GetAttributeFallbackValue(distributionProperty, &distributionDefault)
      || distributionDefault != pxr::TfToken("ByTriangle")) {
    std::cerr << "Spawn distribution must default to ByTriangle.\n";
    return -1;
  }
  pxr::VtArray<pxr::TfToken> allowedDistributions;
  if (!primDef->GetPropertyMetadata(distributionProperty, pxr::TfToken("allowedTokens"), &allowedDistributions)
      || allowedDistributions.size() != 2
      || allowedDistributions[0] != pxr::TfToken("ByTriangle")
      || allowedDistributions[1] != pxr::TfToken("BySurfaceArea")) {
    std::cerr << "Spawn distribution must expose ByTriangle and BySurfaceArea.\n";
    return -1;
  }

  const auto schemaLayer = pxr::SdfLayer::FindOrOpen((pluginDir.parent_path() / "generatedSchema.usda").string());
  if (!schemaLayer) {
    std::cerr << "ParticleSystemAPI schema layer could not be opened.\n";
    return -1;
  }
  return testScreenSizeSchemaAndAuthoredValues(*primDef, schemaLayer) ? 0 : -1;
}

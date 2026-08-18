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
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include "../../../src/lssusd/usd_include_end.h"


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

  return 0;
}

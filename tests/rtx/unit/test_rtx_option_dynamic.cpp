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
#include "../../../src/dxvk/rtx_render/rtx_option.h"
#include "../../../src/dxvk/rtx_render/rtx_option_dynamic.h"
#include "../../../src/dxvk/rtx_render/rtx_option_layer.h"
#include "../../../src/dxvk/rtx_render/rtx_option_manager.h"
#include "../../../src/util/config/config.h"

#include "../../test_utils.h"

#include <filesystem>
#include <iterator>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <atomic>
#include <thread>

namespace dxvk {
  // Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_rtx_option_dynamic.log");

namespace rtx_option_dynamic_test {

  #define TEST_ASSERT(condition, message) \
    do { \
      if (!(condition)) { \
        std::ostringstream oss; \
        oss << "FAILED: " << __FUNCTION__ << " line " << __LINE__ << ": " << message; \
        throw DxvkError(oss.str()); \
      } \
    } while(0)

  namespace fs = std::filesystem;

  // The option registry cannot be reset, so every test uses its own namespace prefix.
  DynamicOptionNamespace makeNamespace(const std::string& prefix, OptionType type, const std::string& defaultValue, uint32_t flags = 0) {
    DynamicOptionNamespace ns;
    ns.prefix = prefix;
    ns.type = type;
    ns.defaultValue = defaultValue;
    ns.flags = flags;
    ns.description = "Test namespace";
    return ns;
  }

  fs::path confPath(const char* name) {
    return fs::temp_directory_path() / (std::string("rtx_option_dynamic_") + name + ".conf");
  }

  void writeFile(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::trunc);
    file << text;
  }

  std::string readFile(const fs::path& path) {
    std::ifstream file(path);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
  }

  // Returns the value text for key in a saved config file, or "<missing>".
  std::string savedValue(const fs::path& path, const std::string& key) {
    std::istringstream lines(readFile(path));
    std::string line;
    const std::string linePrefix = key + " = ";
    while (std::getline(lines, line)) {
      if (line.compare(0, linePrefix.size(), linePrefix) == 0) {
        return line.substr(linePrefix.size());
      }
    }
    return "<missing>";
  }

  RtxOptionLayer* acquireFileLayer(const fs::path& path, uint32_t priority, const char* name) {
    return RtxOptionManager::acquireLayer(path.string(), RtxOptionLayerKey { priority, name }, 1.0f, 0.1f, false, nullptr);
  }

  RtxOptionLayer* acquireConfigLayer(const Config& config, uint32_t priority, const char* name, float blendStrength = 1.0f) {
    return RtxOptionManager::acquireLayer("", RtxOptionLayerKey { priority, name }, blendStrength, 0.1f, false, &config);
  }

  void resolve() {
    RtxOptionManager::applyPendingValues(nullptr, false);
  }

  bool exists(const std::string& key) {
    return RtxOptionManager::getDynamicValue(key).has_value();
  }

  template<typename T>
  T getValue(const std::string& key) {
    const std::optional<DynamicOptionValue> value = RtxOptionManager::getDynamicValue(key);
    TEST_ASSERT(value.has_value(), "missing dynamic option " << key);
    TEST_ASSERT(std::holds_alternative<T>(*value), "unexpected type for " << key);
    return std::get<T>(*value);
  }

  void expectQueue(const std::string& key, const std::string& value, DynamicOptionResult expected) {
    const DynamicOptionResult result = RtxOptionManager::queueDynamicValue(key, value);
    TEST_ASSERT(result == expected, "queueDynamicValue(" << key << ", '" << value << "') returned " << static_cast<int>(result));
  }

  // ============================================================================

  void registerBeforeInitialization() {
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.early.", OptionType::Float, "1")), "early registration");
    expectQueue("rtx.dyntest.early.value", "2.5", DynamicOptionResult::Success);
  }

  void test_valuesQueuedBeforeInitialization() {
    TEST_ASSERT(getValue<float>("rtx.dyntest.early.value") == 2.5f, "queued value applied at first drain");
  }

  void test_registrationValidation() {
    const auto ns = makeNamespace("rtx.dyntest.reg.", OptionType::Int, "3");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(ns), "valid namespace");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(ns), "identical registration is idempotent");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.reg.", OptionType::Int, "4")), "conflicting definition");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.reg.sub.", OptionType::Int, "0")), "nested prefix overlaps");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.", OptionType::Int, "0")), "parent prefix overlaps");

    for (const char* prefix : { "rtx.dyntest.noDot", "foo.dyntest.", "rtx..dyntest.", "rtx.dyn-test.", "rtx.", "rtx.dyntest..x." }) {
      TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace(prefix, OptionType::Int, "0")), "malformed prefix " << prefix);
    }

    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.baddef1.", OptionType::Int, "abc")), "invalid int default");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.baddef2.", OptionType::Float, "nan")), "invalid float default");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.baddef3.", OptionType::Bool, "yes")), "invalid bool default");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.emptystr.", OptionType::String, "")), "empty string default");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.hashset.", OptionType::HashSet, "")), "unsupported type");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.noreset.", OptionType::Int, "0", RtxOptionFlags::NoReset)), "NoReset rejected");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.invalidates.", OptionType::Int, "0", RtxOptionFlags::InvalidatesDrawcallTranslation)), "InvalidatesDrawcallTranslation rejected");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.flags.", OptionType::Int, "0", RtxOptionFlags::UserSetting | RtxOptionFlags::NoSave)), "allowed flags");

    // A namespace may not cover a compile-time option.
    std::string staticPrefix;
    const auto options = RtxOptionImpl::getGlobalOptionMap();
    for (const auto& [hash, option] : *options) {
      const std::string fullName = option->getFullName();
      const size_t lastDot = fullName.rfind('.');
      if (!option->isDynamic() && fullName.compare(0, 4, "rtx.") == 0 && lastDot > 4) {
        staticPrefix = fullName.substr(0, lastDot + 1);
        break;
      }
    }
    TEST_ASSERT(!staticPrefix.empty(), "found a static option category");
    TEST_ASSERT(!RtxOptionManager::registerDynamicNamespace(makeNamespace(staticPrefix, OptionType::Int, "0")), "prefix covering static option " << staticPrefix);
  }

  void test_grammar() {
    const std::string p = "rtx.dyntest.gram.";
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(p + "f.", OptionType::Float, "0")), "float ns");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(p + "i.", OptionType::Int, "0")), "int ns");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(p + "b.", OptionType::Bool, "false")), "bool ns");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(p + "s.", OptionType::String, "")), "string ns");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(p + "v3.", OptionType::Vector3, "0, 0, 0")), "vector3 ns");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(p + "v2i.", OptionType::Vector2i, "0, 0")), "vector2i ns");

    const auto ok = DynamicOptionResult::Success;
    const auto bad = DynamicOptionResult::InvalidArgument;
    for (const char* v : { "1.5", " 2.5 ", "+3", "1e3", "-0.25" }) { expectQueue(p + "f.x", v, ok); }
    for (const char* v : { "1.5abc", "nan", "inf", "1e39", "", "0x10", "1..2" }) { expectQueue(p + "f.x", v, bad); }
    for (const char* v : { "42", "-7", "+8" }) { expectQueue(p + "i.x", v, ok); }
    for (const char* v : { "12abc", "1.5", "4294967296", "" }) { expectQueue(p + "i.x", v, bad); }
    for (const char* v : { "True", "false", "1", "0" }) { expectQueue(p + "b.x", v, ok); }
    for (const char* v : { "yes", "" }) { expectQueue(p + "b.x", v, bad); }
    for (const char* v : { "hello", "with inner space", " lead", "trail ", "path/to.file" }) { expectQueue(p + "s.x", v, ok); }
    for (const char* v : { "", "   ", "a\"b", "a\nb", "tab\tinside" }) { expectQueue(p + "s.x", v, bad); }
    expectQueue(p + "s.padded", " \tpadded value  ", ok);
    for (const char* v : { "1, 2, 3", "1,2,3" }) { expectQueue(p + "v3.x", v, ok); }
    for (const char* v : { "1,2", "1,2,3,4", "5", "1,2,", "1,,3" }) { expectQueue(p + "v3.x", v, bad); }
    expectQueue(p + "v2i.x", "3, -4", ok);
    expectQueue(p + "v2i.x", "3.5, 4", bad);

    expectQueue(p + "f.a.b", "1", ok);
    for (const char* key : { "f.bad-key", "f..x", "f.x.", "f.sp ace", "f." }) { expectQueue(p + key, "1", bad); }
    TEST_ASSERT(RtxOptionManager::setDynamicValue(p + "f.", "1") == bad, "set on namespace prefix rejected");
    TEST_ASSERT(RtxOptionManager::clearDynamicValue(p + "f.") == bad, "clear on namespace prefix rejected");
    expectQueue("rtx.dyntest.nope.x", "1", DynamicOptionResult::NotOwned);
    expectQueue("rtx.dyntest.GRAM.f.x", "1", DynamicOptionResult::NotOwned);

    // Values apply in submission order.
    expectQueue(p + "i.order", "1", ok);
    expectQueue(p + "i.order", "2", ok);
    resolve();
    TEST_ASSERT(getValue<int32_t>(p + "i.order") == 2, "last queued value wins");
    TEST_ASSERT(getValue<float>(p + "f.x") == -0.25f, "float value");
    TEST_ASSERT(getValue<int32_t>(p + "i.x") == 8, "int value with plus sign");
    TEST_ASSERT(getValue<bool>(p + "b.x") == false, "bool value");
    TEST_ASSERT(getValue<std::string>(p + "s.x") == "path/to.file", "string value");
    TEST_ASSERT(getValue<std::string>(p + "s.padded") == "padded value", "string value is trimmed");
    TEST_ASSERT(getValue<Vector3>(p + "v3.x") == Vector3(1.0f, 2.0f, 3.0f), "vector3 value");
    TEST_ASSERT(getValue<Vector2i>(p + "v2i.x") == Vector2i(3, -4), "vector2i value");
    TEST_ASSERT(!exists(p + "f.bad-key"), "invalid key not created");
  }

  void test_roundTrip() {
    struct Case { OptionType type; const char* name; const char* defaultValue; const char* value; };
    const Case cases[] = {
      { OptionType::Bool, "b", "false", "True" },
      { OptionType::Int, "i", "0", "-12" },
      { OptionType::Float, "f", "0", "0.1" },
      { OptionType::String, "s", "", "hello_world.v1" },
      { OptionType::Vector2, "v2", "0, 0", "1.5, -2" },
      { OptionType::Vector3, "v3", "0, 0, 0", "1, 2.25, -3" },
      { OptionType::Vector4, "v4", "0, 0, 0, 0", "0.5, 0.25, 0.125, 1" },
      { OptionType::Vector2i, "v2i", "0, 0", "3, -4" },
    };
    const fs::path path = confPath("roundtrip");
    std::string text;
    for (const Case& c : cases) {
      const std::string prefix = std::string("rtx.dyntest.rt.") + c.name + ".";
      TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(prefix, c.type, c.defaultValue)), "round trip ns " << prefix);
      text += prefix + "key = " + c.value + "\n";
    }
    writeFile(path, text);

    RtxOptionLayer* layer = acquireFileLayer(path, 1100, "RoundTripLayer");
    resolve();
    TEST_ASSERT(getValue<bool>("rtx.dyntest.rt.b.key") == true, "bool loaded");
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.rt.i.key") == -12, "int loaded");
    TEST_ASSERT(getValue<float>("rtx.dyntest.rt.f.key") == 0.1f, "float loaded");
    TEST_ASSERT(getValue<std::string>("rtx.dyntest.rt.s.key") == "hello_world.v1", "string loaded");
    TEST_ASSERT(getValue<Vector2>("rtx.dyntest.rt.v2.key") == Vector2(1.5f, -2.0f), "vector2 loaded");
    TEST_ASSERT(getValue<Vector3>("rtx.dyntest.rt.v3.key") == Vector3(1.0f, 2.25f, -3.0f), "vector3 loaded");
    TEST_ASSERT(getValue<Vector4>("rtx.dyntest.rt.v4.key") == Vector4(0.5f, 0.25f, 0.125f, 1.0f), "vector4 loaded");
    TEST_ASSERT(getValue<Vector2i>("rtx.dyntest.rt.v2i.key") == Vector2i(3, -4), "vector2i loaded");

    TEST_ASSERT(layer->save(), "first save");
    std::vector<std::string> firstSave;
    for (const Case& c : cases) {
      firstSave.push_back(savedValue(path, std::string("rtx.dyntest.rt.") + c.name + ".key"));
    }
    TEST_ASSERT(layer->reload(), "reload");
    resolve();
    TEST_ASSERT(getValue<float>("rtx.dyntest.rt.f.key") == 0.1f, "float after reload");
    TEST_ASSERT(getValue<Vector4>("rtx.dyntest.rt.v4.key") == Vector4(0.5f, 0.25f, 0.125f, 1.0f), "vector4 after reload");
    TEST_ASSERT(layer->save(), "second save");
    for (size_t i = 0; i < std::size(cases); ++i) {
      const std::string key = std::string("rtx.dyntest.rt.") + cases[i].name + ".key";
      TEST_ASSERT(firstSave[i] != "<missing>", "saved " << key);
      TEST_ASSERT(savedValue(path, key) == firstSave[i], "stable text for " << key << ": " << firstSave[i]);
    }

    RtxOptionManager::releaseLayer(layer);
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.rt.i.key") == 0, "default after release");
    fs::remove(path);
  }

  void test_layerPrecedence() {
    const std::string key = "rtx.dyntest.prec.k";
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.prec.", OptionType::Float, "0")), "prec ns");
    const fs::path weakPath = confPath("prec_weak");
    const fs::path strongPath = confPath("prec_strong");
    writeFile(weakPath, key + " = 1.0\n");
    writeFile(strongPath, key + " = abc\n");

    RtxOptionLayer* weak = acquireFileLayer(weakPath, 1200, "PrecWeak");
    RtxOptionLayer* strong = acquireFileLayer(strongPath, 1300, "PrecStrong");
    resolve();
    TEST_ASSERT(getValue<float>(key) == 1.0f, "invalid strong value does not mask weak value");

    writeFile(strongPath, key + " = 3.0\n");
    strong->reload();
    resolve();
    TEST_ASSERT(getValue<float>(key) == 3.0f, "valid strong value wins");

    writeFile(strongPath, key + " = bad\n");
    strong->reload();
    resolve();
    TEST_ASSERT(getValue<float>(key) == 1.0f, "stale strong value withdrawn after invalid edit");

    writeFile(strongPath, key + " = 3.0\n");
    strong->reload();
    resolve();
    RtxOptionManager::releaseLayer(strong);
    resolve();
    TEST_ASSERT(getValue<float>(key) == 1.0f, "removing strong layer reveals weak value");
    RtxOptionManager::releaseLayer(weak);
    resolve();
    TEST_ASSERT(getValue<float>(key) == 0.0f, "default after all layers removed");

    // A zero-blend layer is still an opinion.
    Config zeroBlend;
    zeroBlend.setOption("rtx.dyntest.prec.z", std::string("5"));
    RtxOptionLayer* zeroLayer = acquireConfigLayer(zeroBlend, 1250, "PrecZeroBlend", 0.0f);
    resolve();
    const auto withOpinions = RtxOptionManager::enumerateDynamicOptions("rtx.dyntest.prec.", true);
    TEST_ASSERT(withOpinions.size() == 1 && withOpinions[0].suffix == "z", "zero-blend opinion counts");
    TEST_ASSERT(getValue<float>("rtx.dyntest.prec.z") == 0.0f, "zero-blend layer does not contribute");
    RtxOptionManager::releaseLayer(zeroLayer);
    resolve();

    fs::remove(weakPath);
    fs::remove(strongPath);
  }

  void test_lateRegistration() {
    const fs::path path = confPath("late");
    writeFile(path, "rtx.dyntest.late.a = 4\nrtx.dyntest.late.b = oops\n");
    RtxOptionLayer* layer = acquireFileLayer(path, 1400, "LateLayer");
    resolve();
    TEST_ASSERT(!exists("rtx.dyntest.late.a"), "no option before registration");

    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.late.", OptionType::Int, "0")), "late ns");
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.late.a") == 4, "resolved synchronously on registration");
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.late.b") == 0, "rejected value falls back to default");
    TEST_ASSERT(layer->isKeyRejected("rtx.dyntest.late.b"), "rejected key recorded");
    TEST_ASSERT(!layer->hasUnsavedChanges(), "no false unsaved changes after replay");

    // Disabled layers are not replayed; their keys appear once the layer is enabled.
    const fs::path disabledPath = confPath("late_disabled");
    writeFile(disabledPath, "rtx.dyntest.late2.c = 7\n");
    RtxOptionLayer* disabled = acquireFileLayer(disabledPath, 1450, "LateDisabled");
    disabled->requestEnabled(false);
    resolve();
    TEST_ASSERT(!disabled->isEnabled(), "layer disabled");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.late2.", OptionType::Int, "0")), "late2 ns");
    TEST_ASSERT(!exists("rtx.dyntest.late2.c"), "disabled layer not replayed");
    disabled->requestEnabled(true);
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.late2.c") == 7, "value appears when layer is enabled");

    RtxOptionManager::releaseLayer(disabled);
    RtxOptionManager::releaseLayer(layer);
    resolve();
    fs::remove(path);
    fs::remove(disabledPath);
  }

  void test_discovery() {
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.disc.", OptionType::Int, "0")), "disc ns");
    const fs::path path = confPath("discovery");
    writeFile(path, "rtx.dyntest.disc.a = 1\n");
    RtxOptionLayer* layer = acquireFileLayer(path, 1500, "DiscoveryLayer");
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.disc.a") == 1, "discovered on acquire");
    TEST_ASSERT(!exists("rtx.dyntest.disc.b"), "b not yet present");

    writeFile(path, "rtx.dyntest.disc.a = 1\nrtx.dyntest.disc.b = 2\n");
    layer->reload();
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.disc.b") == 2, "discovered on reload");

    RtxOptionManager::releaseLayer(layer);
    resolve();
    fs::remove(path);
  }

  void test_preservation() {
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.pres.", OptionType::Int, "0")), "pres ns");
    const fs::path path = confPath("preservation");
    writeFile(path, "rtx.dyntest.unregistered.x = 1\nrtx.dyntest.pres.bad = notanumber\nrtx.dyntest.pres.good = 3\n");
    RtxOptionLayer* layer = acquireFileLayer(path, 1600, "PreservationLayer");
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.pres.good") == 3, "valid value loaded");
    layer->onLayerValueChanged();
    TEST_ASSERT(!layer->hasUnsavedChanges(), "unmanaged and rejected keys are not pending removals");

    TEST_ASSERT(layer->save(), "save");
    TEST_ASSERT(savedValue(path, "rtx.dyntest.unregistered.x") == "1", "unmanaged key preserved");
    TEST_ASSERT(savedValue(path, "rtx.dyntest.pres.bad") == "notanumber", "rejected value preserved");
    TEST_ASSERT(savedValue(path, "rtx.dyntest.pres.good") == "3", "managed value saved");

    RtxOptionImpl* good = RtxOptionImpl::getOptionByFullName("rtx.dyntest.pres.good");
    TEST_ASSERT(good != nullptr, "good option exists");
    good->disableLayerValue(layer);
    layer->onLayerValueChanged();
    TEST_ASSERT(layer->hasUnsavedChanges(), "intentional removal is pending");
    TEST_ASSERT(layer->save(), "save after removal");
    TEST_ASSERT(savedValue(path, "rtx.dyntest.pres.good") == "<missing>", "intentional removal stays removed");
    TEST_ASSERT(savedValue(path, "rtx.dyntest.pres.bad") == "notanumber", "rejected value still preserved");

    writeFile(path, "rtx.dyntest.unregistered.x = 1\nrtx.dyntest.pres.bad = 5\n");
    layer->reload();
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.pres.bad") == 5, "replacement value loaded");
    TEST_ASSERT(!layer->isKeyRejected("rtx.dyntest.pres.bad"), "replacement clears rejection");
    TEST_ASSERT(layer->save(), "save after replacement");
    TEST_ASSERT(savedValue(path, "rtx.dyntest.pres.bad") == "5", "replacement saved");

    RtxOptionManager::releaseLayer(layer);
    resolve();
    fs::remove(path);
  }

  void test_enumeration() {
    const std::string prefix = "rtx.dyntest.enum.";
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(prefix, OptionType::String, "")), "enum ns");
    Config config;
    config.setOption(prefix + "b", std::string("two"));
    config.setOption(prefix + "a", std::string("one"));
    config.setOption(prefix + "c.d", std::string("three"));
    config.setOption(prefix + "e", std::string("bad\"quote"));
    RtxOptionLayer* layer = acquireConfigLayer(config, 1700, "EnumerationLayer");
    resolve();

    const auto all = RtxOptionManager::enumerateDynamicOptions(prefix, false);
    TEST_ASSERT(all.size() == 4, "all options: " << all.size());
    TEST_ASSERT(all[0].suffix == "a" && all[1].suffix == "b" && all[2].suffix == "c.d" && all[3].suffix == "e", "sorted by full name");
    TEST_ASSERT(std::get<std::string>(all[0].value) == "one", "value copied");

    const auto withOpinions = RtxOptionManager::enumerateDynamicOptions(prefix, true);
    TEST_ASSERT(withOpinions.size() == 3, "rejected value is not an opinion: " << withOpinions.size());

    auto copy = RtxOptionManager::enumerateDynamicOptions(prefix, false);
    std::get<std::string>(copy[0].value) = "changed";
    TEST_ASSERT(getValue<std::string>(prefix + "a") == "one", "enumeration returns copies");

    for (const DynamicOptionEntry& entry : RtxOptionManager::enumerateDynamicOptions("rtx.", false)) {
      const RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName("rtx." + entry.suffix);
      TEST_ASSERT(option != nullptr && option->isDynamic(), "only dynamic options enumerated: " << entry.suffix);
    }

    RtxOptionManager::releaseLayer(layer);
    resolve();
  }

  void test_concurrentRegistration() {
    Config config;
    config.setOption("rtx.dyntest.conc.k", std::string("9"));
    RtxOptionLayer* layer = acquireConfigLayer(config, 1800, "ConcurrencyLayer");
    resolve();

    const RtxOptionImpl::RtxOptionMapSnapshot held = RtxOptionImpl::getGlobalOptionMap();
    const size_t heldSize = held->size();
    std::thread worker([] {
      RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.conc.", OptionType::Int, "0"));
      RtxOptionManager::queueDynamicValue("rtx.dyntest.conc.q", "1");
      RtxOptionManager::queueDynamicValue("rtx.dyntest.conc.q", "5");
    });
    // Iterating the held snapshot while the worker publishes must stay valid.
    size_t iterated = 0;
    for (const auto& entry : *held) {
      iterated += entry.second != nullptr ? 1 : 0;
    }
    worker.join();

    TEST_ASSERT(iterated == heldSize && held->size() == heldSize, "held snapshot is immutable");
    const XXH64_hash_t hash = StringToXXH64(std::string("rtx.dyntest.conc.k"), 0);
    TEST_ASSERT(held->find(hash) == held->end(), "held snapshot does not see new option");
    TEST_ASSERT(RtxOptionImpl::getGlobalOptionMap()->count(hash) == 1, "new snapshot contains new option");
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.conc.k") == 9, "registration from another thread resolved");
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.conc.q") == 5, "queued values applied in order");

    RtxOptionManager::releaseLayer(layer);
    resolve();
  }

  void test_exactPrefixKey() {
    const std::string prefix = "rtx.dyntest.exact.";
    Config before;
    before.setOption(prefix, std::string("1"));
    before.setOption(prefix + "ok", std::string("2"));
    RtxOptionLayer* beforeLayer = acquireConfigLayer(before, 2050, "ExactPrefixBefore");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace(prefix, OptionType::Int, "0")), "exact ns");
    TEST_ASSERT(getValue<int32_t>(prefix + "ok") == 2, "valid key replayed");
    TEST_ASSERT(RtxOptionImpl::getOptionByFullName(prefix) == nullptr, "prefix key not created on late registration");

    Config after;
    after.setOption(prefix, std::string("3"));
    RtxOptionLayer* afterLayer = acquireConfigLayer(after, 2060, "ExactPrefixAfter");
    resolve();
    TEST_ASSERT(RtxOptionImpl::getOptionByFullName(prefix) == nullptr, "prefix key not created on discovery");

    RtxOptionManager::releaseLayer(afterLayer);
    RtxOptionManager::releaseLayer(beforeLayer);
    resolve();
  }

  void test_registrationDuringResolve() {
    static constexpr int kNamespaces = 16;
    Config config;
    for (int i = 0; i < kNamespaces; ++i) {
      config.setOption("rtx.dyntest.overlap" + std::to_string(i) + ".k", std::to_string(i));
    }
    RtxOptionLayer* layer = acquireConfigLayer(config, 1850, "OverlapLayer");
    resolve();

    // Exercises registration replay (which marks options dirty) overlapping applyPendingValues; it cannot prove race freedom.
    std::atomic<bool> done { false };
    std::thread worker([&done] {
      for (int i = 0; i < kNamespaces; ++i) {
        RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.overlap" + std::to_string(i) + ".", OptionType::Int, "0"));
      }
      done = true;
    });
    while (!done) {
      resolve();
    }
    worker.join();
    resolve();

    for (int i = 0; i < kNamespaces; ++i) {
      TEST_ASSERT(getValue<int32_t>("rtx.dyntest.overlap" + std::to_string(i) + ".k") == i, "overlapping registration " << i);
    }
    RtxOptionManager::releaseLayer(layer);
    resolve();
  }

  void test_disabledLayerEdits() {
    const std::string key = "rtx.dyntest.disabled.k";
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.disabled.", OptionType::Int, "0")), "disabled ns");
    Config config;
    config.setOption(key, std::string("3"));
    RtxOptionLayer* layer = acquireConfigLayer(config, 2100, "DisabledEditsLayer");
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 3, "layer value");

    layer->requestEnabled(false);
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 0, "disabled layer removed");
    TEST_ASSERT(RtxOptionManager::setDynamicValue(key, "5", layer) == DynamicOptionResult::NoTargetLayer, "set on disabled layer rejected");
    TEST_ASSERT(RtxOptionManager::clearDynamicValue(key, layer) == DynamicOptionResult::NoTargetLayer, "clear on disabled layer rejected");
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 0, "rejected edits do not apply");

    layer->requestEnabled(true);
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 3, "re-enabled layer keeps its values");

    // A queued edit is dropped if its layer is disabled before the drain.
    TEST_ASSERT(RtxOptionManager::setDynamicValue(key, "7", layer) == DynamicOptionResult::Success, "set on enabled layer");
    layer->requestEnabled(false);
    resolve();
    layer->requestEnabled(true);
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 3, "edit queued before disabling is dropped");

    RtxOptionManager::releaseLayer(layer);
    resolve();
  }

  void test_setAndClear() {
    const std::string key = "rtx.dyntest.edit.k";
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.edit.", OptionType::Int, "0")), "edit ns");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.edituser.", OptionType::Int, "0", RtxOptionFlags::UserSetting)), "edituser ns");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.editnosave.", OptionType::Int, "0", RtxOptionFlags::NoSave)), "editnosave ns");

    // Routing follows the namespace flags and the edit target, like RtxOption setters.
    {
      RtxOptionLayerTarget target(RtxOptionEditTarget::User);
      TEST_ASSERT(RtxOptionManager::setDynamicValue(key, "5") == DynamicOptionResult::Success, "set flagless");
      TEST_ASSERT(RtxOptionManager::setDynamicValue("rtx.dyntest.edituser.k", "6") == DynamicOptionResult::Success, "set UserSetting");
      TEST_ASSERT(RtxOptionManager::setDynamicValue("rtx.dyntest.editnosave.k", "7") == DynamicOptionResult::Success, "set NoSave");
    }
    {
      RtxOptionLayerTarget target(RtxOptionEditTarget::Derived);
      TEST_ASSERT(RtxOptionManager::setDynamicValue("rtx.dyntest.edituser.derived", "8") == DynamicOptionResult::Success, "set derived UserSetting");
    }
    TEST_ASSERT(!exists(key), "set is deferred until applyPendingValues");
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 5, "flagless value");
    TEST_ASSERT(RtxOptionImpl::getOptionByFullName(key)->hasValueInLayer(RtxOptionLayer::getRtxConfLayer()), "flagless user edit goes to rtx.conf layer");
    TEST_ASSERT(RtxOptionImpl::getOptionByFullName("rtx.dyntest.edituser.k")->hasValueInLayer(RtxOptionLayer::getUserLayer()), "UserSetting user edit goes to user layer");
    TEST_ASSERT(RtxOptionImpl::getOptionByFullName("rtx.dyntest.editnosave.k")->hasValueInLayer(RtxOptionLayer::getDerivedLayer()), "NoSave goes to derived layer");
    TEST_ASSERT(RtxOptionImpl::getOptionByFullName("rtx.dyntest.edituser.derived")->hasValueInLayer(RtxOptionLayer::getQualityLayer()), "UserSetting code edit goes to quality layer");

    // Clear reveals the next layer down; requests apply in submission order.
    Config weakConfig;
    weakConfig.setOption(key, std::string("1"));
    RtxOptionLayer* weak = acquireConfigLayer(weakConfig, 1900, "EditWeak");
    RtxOptionLayer* strong = acquireConfigLayer(Config(), 1950, "EditStrong");
    RtxOptionManager::clearDynamicValue(key, RtxOptionLayer::getRtxConfLayer());
    RtxOptionManager::setDynamicValue(key, "7", strong);
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 7, "strong value");
    RtxOptionManager::setDynamicValue(key, "2", strong);
    RtxOptionManager::clearDynamicValue(key, strong);
    RtxOptionManager::setDynamicValue(key, "3", strong);
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 3, "set, clear, set applies in order");
    RtxOptionManager::setDynamicValue(key, "4", strong);
    RtxOptionManager::clearDynamicValue(key, strong);
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 1, "clear reveals weaker layer");

    // A clear queued after a set that creates the option in the same drain removes the value.
    RtxOptionManager::setDynamicValue("rtx.dyntest.edit.fresh", "6", strong);
    RtxOptionManager::clearDynamicValue("rtx.dyntest.edit.fresh", strong);
    resolve();
    TEST_ASSERT(!RtxOptionManager::getDynamicValue("rtx.dyntest.edit.fresh", true).has_value(), "clear of a new option in the same drain");

    // API writes share the queue.
    expectQueue(key, "9", DynamicOptionResult::Success);
    RtxOptionManager::clearDynamicValue(key, RtxOptionLayer::getUserLayer());
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 1, "clear after SetConfigVariable applies in order");

    // A request for a layer released before the drain is dropped safely.
    RtxOptionLayer* temporary = acquireConfigLayer(Config(), 1990, "EditTemporary");
    RtxOptionManager::setDynamicValue(key, "11", temporary);
    RtxOptionManager::releaseLayer(temporary);
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 1, "request for released layer dropped");

    TEST_ASSERT(RtxOptionManager::setDynamicValue(key, "abc") == DynamicOptionResult::InvalidArgument, "invalid value rejected");
    TEST_ASSERT(RtxOptionManager::clearDynamicValue("rtx.dyntest.edit.bad-key") == DynamicOptionResult::InvalidArgument, "invalid key rejected");
    TEST_ASSERT(RtxOptionManager::setDynamicValue(key, "1", RtxOptionLayer::getDefaultLayer()) == DynamicOptionResult::NoTargetLayer, "default layer not editable");

    RtxOptionManager::releaseLayer(strong);
    RtxOptionManager::releaseLayer(weak);
    resolve();
  }

  void test_clearRejectedValue() {
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.clearbad.", OptionType::Int, "0")), "clearbad ns");
    const std::string key = "rtx.dyntest.clearbad.k";
    const fs::path path = confPath("clear_rejected");
    writeFile(path, key + " = notanumber\n");
    RtxOptionLayer* layer = acquireFileLayer(path, 2000, "ClearRejectedLayer");
    resolve();
    TEST_ASSERT(layer->isKeyRejected(key), "value rejected");
    TEST_ASSERT(getValue<int32_t>(key) == 0, "default value returned");
    TEST_ASSERT(!RtxOptionManager::getDynamicValue(key, true).has_value(), "no opinion for rejected value");

    RtxOptionManager::clearDynamicValue(key, layer);
    resolve();
    TEST_ASSERT(!layer->isKeyRejected(key), "clear discards rejection");
    TEST_ASSERT(layer->hasUnsavedChanges(), "cleared line is a pending removal");
    TEST_ASSERT(layer->save(), "save");
    TEST_ASSERT(savedValue(path, key) == "<missing>", "cleared rejected line removed on save");

    RtxOptionManager::releaseLayer(layer);
    resolve();
    fs::remove(path);
  }

  void test_stringTrimFromFile() {
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.strtrim.", OptionType::String, "")), "strtrim ns");
    const std::string key = "rtx.dyntest.strtrim.k";
    const fs::path path = confPath("string_trim");
    // The config parser keeps trailing whitespace.
    writeFile(path, key + " = Ultra  \n");
    RtxOptionLayer* layer = acquireFileLayer(path, 2150, "StringTrimLayer");
    resolve();
    TEST_ASSERT(!layer->isKeyRejected(key), "trailing whitespace accepted");
    TEST_ASSERT(getValue<std::string>(key) == "Ultra", "value trimmed");
    TEST_ASSERT(layer->save(), "save");
    TEST_ASSERT(savedValue(path, key) == "Ultra", "trimmed value saved");

    RtxOptionManager::releaseLayer(layer);
    resolve();
    fs::remove(path);
  }

  void test_queuedEditsFollowLayerGeneration() {
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.gen.", OptionType::Int, "0")), "gen ns");
    const std::string key = "rtx.dyntest.gen.k";
    const fs::path path = confPath("generation");
    writeFile(path, key + " = 3\n");
    RtxOptionLayer* layer = acquireFileLayer(path, 2200, "GenerationLayer");
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 3, "disk value");

    // Reload discards edits queued before it.
    TEST_ASSERT(RtxOptionManager::setDynamicValue(key, "7", layer) == DynamicOptionResult::Success, "queue set");
    layer->reload();
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 3, "set queued before reload dropped");
    layer->onLayerValueChanged();
    TEST_ASSERT(!layer->hasUnsavedChanges(), "layer not dirty after reload");

    TEST_ASSERT(RtxOptionManager::clearDynamicValue(key, layer) == DynamicOptionResult::Success, "queue clear");
    layer->reload();
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 3, "clear queued before reload dropped");

    // Requests from before and after a reload in one drain: only the newer one applies.
    RtxOptionManager::setDynamicValue(key, "8", layer);
    layer->reload();
    RtxOptionManager::setDynamicValue(key, "9", layer);
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 9, "edit queued after reload applies");

    // Clearing the layer also discards queued edits.
    RtxOptionManager::setDynamicValue(key, "5", layer);
    layer->removeFromAllOptions();
    resolve();
    TEST_ASSERT(getValue<int32_t>(key) == 0, "set queued before clear dropped");
    layer->reload();
    resolve();

    // Edits never reach a replacement layer with the same key.
    const RtxOptionLayerKey replaceKey { 2250, "GenerationReplace" };
    Config original;
    original.setOption("rtx.dyntest.gen.r", std::string("1"));
    Config replacementConfig;
    replacementConfig.setOption("rtx.dyntest.gen.r", std::string("2"));
    RtxOptionLayer* first = RtxOptionManager::acquireLayer("", replaceKey, 1.0f, 0.1f, false, &original);
    resolve();
    RtxOptionManager::setDynamicValue("rtx.dyntest.gen.r", "7", first);
    RtxOptionManager::releaseLayer(first);
    RtxOptionLayer* replacement = RtxOptionManager::acquireLayer("", replaceKey, 1.0f, 0.1f, false, &replacementConfig);
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.gen.r") == 2, "set for released layer does not reach replacement");
    RtxOptionManager::clearDynamicValue("rtx.dyntest.gen.r", replacement);
    RtxOptionManager::releaseLayer(replacement);
    replacement = RtxOptionManager::acquireLayer("", replaceKey, 1.0f, 0.1f, false, &replacementConfig);
    resolve();
    TEST_ASSERT(getValue<int32_t>("rtx.dyntest.gen.r") == 2, "clear for released layer does not reach replacement");
    RtxOptionManager::releaseLayer(replacement);

    // SetConfigVariable edits are bound to the user layer's generation.
    const std::string userKey = "rtx.dyntest.gen.user";
    expectQueue(userKey, "4", DynamicOptionResult::Success);
    RtxOptionLayer::getUserLayer()->removeFromAllOptions();
    resolve();
    TEST_ASSERT(!RtxOptionManager::getDynamicValue(userKey, true).has_value(), "API edit queued before user layer reset dropped");
    expectQueue(userKey, "5", DynamicOptionResult::Success);
    resolve();
    TEST_ASSERT(getValue<int32_t>(userKey) == 5, "API edit after reset applies");

    RtxOptionManager::releaseLayer(layer);
    resolve();
    fs::remove(path);
  }

  void test_migrationClearsRejection() {
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.migrate.", OptionType::Int, "0")), "migrate ns");
    const std::string key = "rtx.dyntest.migrate.k";
    const fs::path path = confPath("migration");
    writeFile(path, key + " = notanumber\n");
    RtxOptionLayer* dest = acquireFileLayer(path, 2300, "MigrationDest");
    Config sourceConfig;
    sourceConfig.setOption(key, std::string("5"));
    RtxOptionLayer* source = acquireConfigLayer(sourceConfig, 2350, "MigrationSource");
    resolve();
    TEST_ASSERT(dest->isKeyRejected(key), "destination value rejected");

    RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName(key);
    option->moveLayerValue(source, dest);
    resolve();
    TEST_ASSERT(!dest->isKeyRejected(key), "migrated value clears rejection");
    TEST_ASSERT(getValue<int32_t>(key) == 5, "migrated value");
    TEST_ASSERT(dest->save(), "save");
    TEST_ASSERT(savedValue(path, key) == "5", "migrated value saved over rejected line");

    option->disableLayerValue(dest);
    dest->onLayerValueChanged();
    TEST_ASSERT(dest->hasUnsavedChanges(), "removal is pending");
    TEST_ASSERT(dest->save(), "save after removal");
    TEST_ASSERT(savedValue(path, key) == "<missing>", "removed value not saved");
    dest->reload();
    resolve();
    TEST_ASSERT(!RtxOptionManager::getDynamicValue(key, true).has_value(), "reload does not restore the rejected line");

    // migrateValuesTo clears the rejection only when its transform installs a value.
    const std::string fromKey = "rtx.dyntest.migrate.from";
    const std::string toKey = "rtx.dyntest.migrate.to";
    writeFile(path, fromKey + " = 4\n" + toKey + " = bad\n");
    dest->reload();
    resolve();
    RtxOptionImpl* from = RtxOptionImpl::getOptionByFullName(fromKey);
    RtxOptionImpl* to = RtxOptionImpl::getOptionByFullName(toKey);
    TEST_ASSERT(from != nullptr && to != nullptr && dest->isKeyRejected(toKey), "migration options");
    from->migrateValuesTo(to, [](const GenericValue&, GenericValue&, bool) { return false; });
    TEST_ASSERT(dest->isKeyRejected(toKey), "failed transform keeps rejection");
    from->migrateValuesTo(to, [](const GenericValue& src, GenericValue& dst, bool) { dst.i = src.i; return true; });
    resolve();
    TEST_ASSERT(!dest->isKeyRejected(toKey), "successful transform clears rejection");
    TEST_ASSERT(getValue<int32_t>(toKey) == 4, "transformed value");

    RtxOptionManager::releaseLayer(source);
    RtxOptionManager::releaseLayer(dest);
    resolve();
    fs::remove(path);
  }

  void test_redundantDynamicValues() {
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.dupe.", OptionType::Bool, "true")), "dupe ns");
    TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(makeNamespace("rtx.dyntest.dupef.", OptionType::Float, "1")), "dupef ns");
    const std::string p = "rtx.dyntest.dupe.";
    Config strongConfig;
    for (const char* name : { "onlyDefault", "inactiveBelow", "activeEqual", "activeDifferent" }) {
      strongConfig.setOption(p + name, std::string("True"));
    }
    strongConfig.setOption("rtx.dyntest.dupef.onlyDefault", std::string("1"));
    Config inactiveConfig;
    inactiveConfig.setOption(p + "inactiveBelow", std::string("True"));
    Config activeConfig;
    activeConfig.setOption(p + "activeEqual", std::string("True"));
    activeConfig.setOption(p + "activeDifferent", std::string("False"));
    RtxOptionLayer* inactive = acquireConfigLayer(inactiveConfig, 2400, "DupeInactive", 0.05f);
    RtxOptionLayer* active = acquireConfigLayer(activeConfig, 2410, "DupeActive");
    RtxOptionLayer* strong = acquireConfigLayer(strongConfig, 2420, "DupeStrong");
    resolve();

    Config changed;
    RtxOptionManager::writeOptions(changed, strong, true);
    TEST_ASSERT(changed.findOption((p + "onlyDefault").c_str()), "value equal to the default is a change");
    TEST_ASSERT(!changed.findOption((p + "activeEqual").c_str()), "value equal to an active weaker value is not a change");

    RtxOptionManager::removeRedundantLayerValues(strong);
    auto inStrong = [strong](const std::string& key) {
      return RtxOptionImpl::getOptionByFullName(key)->hasValueInLayer(strong);
    };
    TEST_ASSERT(inStrong(p + "onlyDefault"), "value equal to the default kept");
    TEST_ASSERT(inStrong(p + "inactiveBelow"), "value equal to an inactive weaker value kept");
    TEST_ASSERT(!inStrong(p + "activeEqual"), "value equal to an active weaker value removed");
    TEST_ASSERT(inStrong(p + "activeDifferent"), "value different from an active weaker value kept");
    TEST_ASSERT(inStrong("rtx.dyntest.dupef.onlyDefault"), "float value equal to the default kept");

    RtxOptionManager::releaseLayer(strong);
    RtxOptionManager::releaseLayer(active);
    RtxOptionManager::releaseLayer(inactive);
    resolve();
  }

  void test_metadataLifetime() {
    {
      std::string prefix = std::string("rtx.dyntest.") + "life.";
      DynamicOptionNamespace ns = makeNamespace(prefix, OptionType::String, "");
      ns.description = std::string("Lifetime ") + "description";
      TEST_ASSERT(RtxOptionManager::registerDynamicNamespace(ns), "life ns");
      std::string key = prefix + "name";
      std::string value = "value";
      expectQueue(key, value, DynamicOptionResult::Success);
    }
    resolve();
    const RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName("rtx.dyntest.life.name");
    TEST_ASSERT(option != nullptr, "option exists");
    TEST_ASSERT(std::string(option->getName()) == "name", "owned name");
    TEST_ASSERT(std::string(option->getDescription()) == "Lifetime description", "owned description");
    TEST_ASSERT(option->getFullName() == "rtx.dyntest.life.name", "owned category");
    TEST_ASSERT(getValue<std::string>("rtx.dyntest.life.name") == "value", "owned value");
  }

  void test_documentationExcludesDynamicOptions() {
    const fs::path path = fs::temp_directory_path() / "rtx_option_dynamic_RtxOptions.md";
    TEST_ASSERT(RtxOptionManager::writeMarkdownDocumentation(path.string().c_str()), "documentation written");
    const std::string text = readFile(path);
    TEST_ASSERT(text.find("rtx.dyntest") == std::string::npos, "dynamic options excluded from RtxOptions.md");
    TEST_ASSERT(text.find("| RTX Option |") != std::string::npos, "documentation has option tables");
    fs::remove(path);
  }

  void runAllTests() {
    std::cout << "Running dynamic RtxOption tests" << std::endl;

    registerBeforeInitialization();

    RtxOptionLayer::initializeSystemLayers();
    RtxOptionImpl::setInitialized(true);
    RtxOptionManager::markOptionsWithCallbacksDirty();
    RtxOptionManager::applyPendingValues(nullptr, true);

    test_valuesQueuedBeforeInitialization();
    test_registrationValidation();
    test_grammar();
    test_roundTrip();
    test_layerPrecedence();
    test_lateRegistration();
    test_discovery();
    test_preservation();
    test_enumeration();
    test_concurrentRegistration();
    test_exactPrefixKey();
    test_registrationDuringResolve();
    test_disabledLayerEdits();
    test_setAndClear();
    test_clearRejectedValue();
    test_stringTrimFromFile();
    test_queuedEditsFollowLayerGeneration();
    test_migrationClearsRejection();
    test_redundantDynamicValues();
    test_metadataLifetime();
    test_documentationExcludesDynamicOptions();

    std::cout << "All dynamic RtxOption tests PASSED" << std::endl;
  }

}  // namespace rtx_option_dynamic_test
}  // namespace dxvk

int main() {
  try {
    dxvk::rtx_option_dynamic_test::runAllTests();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& e) {
    std::cerr << "TEST FAILED with exception: " << e.what() << std::endl;
    return -1;
  }
  return 0;
}

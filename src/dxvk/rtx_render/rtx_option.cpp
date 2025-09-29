/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_options.h"

namespace dxvk {
  Config RtxOptionImpl::s_startupOptions;
  Config RtxOptionImpl::s_customOptions;

  void fillHashTable(const std::vector<std::string>& rawInput, fast_unordered_set& hashTableOutput) {
    for (auto&& hashStr : rawInput) {
      const XXH64_hash_t h = std::stoull(hashStr, nullptr, 16);
      hashTableOutput.insert(h);
    }
  }

  void fillHashVector(const std::vector<std::string>& rawInput, std::vector<XXH64_hash_t>& hashVectorOutput) {
    for (auto&& hashStr : rawInput) {
      const XXH64_hash_t h = std::stoull(hashStr, nullptr, 16);
      hashVectorOutput.emplace_back(h);
    }
  }

  void fillIntVector(const std::vector<std::string>& rawInput, std::vector<int32_t>& intVectorOutput) {
    for (auto&& intStr : rawInput) {
      const int32_t i = std::stoi(intStr);
      intVectorOutput.emplace_back(i);
    }
  }

  std::string hashTableToString(const fast_unordered_set& hashTable) {
    std::stringstream ss;
    // Collect elements into a vector for sorting
    std::vector<XXH64_hash_t> sortedHashes(hashTable.begin(), hashTable.end());
    std::sort(sortedHashes.begin(), sortedHashes.end());
    
    for (auto&& hash : sortedHashes) {
      if (ss.tellp() != std::streampos(0))
        ss << ", ";

      ss << "0x" << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
    }
    return ss.str();
  }

  std::string hashVectorToString(const std::vector<XXH64_hash_t>& hashVector) {
    std::stringstream ss;
    for (auto&& hash : hashVector) {
      if (ss.tellp() != std::streampos(0))
        ss << ", ";

      ss << "0x" << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
    }
    return ss.str();
  }
  
  std::string vectorToString(const std::vector<int32_t>& intVector) {
    std::stringstream ss;
    for (auto&& element : intVector) {
      if (ss.tellp() != std::streampos(0))
        ss << ", ";

      ss << element;
    }
    return ss.str();
  }

  struct GenericValueWrapper {
    GenericValueWrapper(OptionType optionType) : type(optionType) {
      construct();

      switch (type) {
      case OptionType::HashSet:
        data.hashSet = &storage.hashSet;
        break;
      case OptionType::HashVector:
        data.hashVector = &storage.hashVector;
        break;
      case OptionType::IntVector:
        data.intVector = &storage.intVector;
        break;
      case OptionType::VirtualKeys:
        data.virtualKeys = &storage.virtualKeys;
        break;
      case OptionType::Vector2:
        data.v2 = &storage.v2;
        break;
      case OptionType::Vector3:
        data.v3 = &storage.v3;
        break;
      case OptionType::Vector2i:
        data.v2i = &storage.v2i;
        break;
      case OptionType::String:
        data.string = &storage.string;
        break;
      case OptionType::Vector4:
        data.v4 = &storage.v4;
        break;
      default:
        data.value = 0;
        break;
      }
    }

    ~GenericValueWrapper() {
      destruct();
    }

    GenericValue data;
  private:
    OptionType type;

    union Storage {
      fast_unordered_set hashSet;
      std::vector<XXH64_hash_t> hashVector;
      std::vector<int32_t> intVector;
      VirtualKeys virtualKeys;
      std::string string;
      Vector2 v2;
      Vector3 v3;
      Vector2i v2i;
      Vector4 v4;

      Storage() { }
      ~Storage() { }
    } storage;

    void construct() {
      switch (type) {
      case OptionType::HashSet:      new (&storage.hashSet) fast_unordered_set(); break;
      case OptionType::HashVector:   new (&storage.hashVector) std::vector<XXH64_hash_t>(); break;
      case OptionType::IntVector:    new (&storage.intVector) std::vector<int32_t>(); break;
      case OptionType::VirtualKeys:  new (&storage.virtualKeys) VirtualKeys(); break;
      case OptionType::String:       new (&storage.string) std::string(); break;
      case OptionType::Vector2:      new (&storage.v2) Vector2(); break;
      case OptionType::Vector3:      new (&storage.v3) Vector3(); break;
      case OptionType::Vector2i:     new (&storage.v2i) Vector2i(); break;
      case OptionType::Vector4:      new (&storage.v4) Vector4(); break;
      default: break;
      }
    }

    void destruct() {
      switch (type) {
      case OptionType::HashSet:      storage.hashSet.~fast_unordered_set(); break;
      case OptionType::HashVector:   storage.hashVector.~vector(); break;
      case OptionType::IntVector:    storage.intVector.~vector(); break;
      case OptionType::VirtualKeys:  storage.virtualKeys.~VirtualKeys(); break;
      case OptionType::String:       storage.string.~basic_string(); break;
      case OptionType::Vector2:      storage.v2.~Vector2(); break;
      case OptionType::Vector3:      storage.v3.~Vector3(); break;
      case OptionType::Vector2i:     storage.v2i.~Vector2i(); break;
      case OptionType::Vector4:      storage.v4.~Vector4(); break;
      default: break;
      }
    }
  };

  GenericValue createGenericValue(OptionType type) {
    GenericValue value;
    switch (type) {
    case OptionType::HashSet:
      value.hashSet = new fast_unordered_set();
      break;
    case OptionType::HashVector:
      value.hashVector = new std::vector<XXH64_hash_t>();
      break;
    case OptionType::IntVector:
      value.intVector = new std::vector<int32_t>();
      break;
    case OptionType::VirtualKeys:
      value.virtualKeys = new VirtualKeys();
      break;
    case OptionType::Vector2:
      value.v2 = new Vector2();
      break;
    case OptionType::Vector3:
      value.v3 = new Vector3();
      break;
    case OptionType::Vector2i:
      value.v2i = new Vector2i();
      break;
    case OptionType::String:
      value.string = new std::string();
      break;
    case OptionType::Vector4:
      value.v4 = new Vector4();
      break;
    default:
      value.value = 0;
      break;
    }

    return value;
  }

  RtxOptionImpl::~RtxOptionImpl() {
    onChangeCallback = nullptr;

    auto releaseValue = [](GenericValue& value, const OptionType type) {
      switch (type) {
      case OptionType::HashSet:
        delete value.hashSet;
        break;
      case OptionType::HashVector:
        delete value.hashVector;
        break;
      case OptionType::IntVector:
        delete value.intVector;
        break;
      case OptionType::VirtualKeys:
        delete value.virtualKeys;
        break;
      case OptionType::Vector2:
        delete value.v2;
        break;
      case OptionType::Vector3:
        delete value.v3;
        break;
      case OptionType::Vector4:
        delete value.v4;
        break;
      case OptionType::Vector2i:
        delete value.v2i;
        break;
      case OptionType::String:
        delete value.string;
        break;
      default:
        break;
      }
    };

    // Release option memory allocated for layers
    for (auto& optionLayer : optionLayerValueQueue) {
      releaseValue(optionLayer.second.value, type);
    }

    for (int i = 0; i < (int)ValueType::Count; i++) {
      releaseValue(valueList[i], type);
    }
  }

  const char* RtxOptionImpl::getTypeString() const {
    switch (type) {
    case OptionType::Bool: return "bool";
    case OptionType::Int: return "int";
    case OptionType::Float: return "float";
    case OptionType::HashSet: return "hash set"; 
    case OptionType::HashVector: return "hash vector";
    case OptionType::IntVector: return "int vector";
    case OptionType::VirtualKeys: return "virtual keys";
    case OptionType::Vector2: return "float2";
    case OptionType::Vector3: return "float3";
    case OptionType::Vector2i: return "int2";
    case OptionType::Vector4: return "float4";
    case OptionType::String: return "string";
    default:
      return "unknown type";
    }
  }

  std::string RtxOptionImpl::genericValueToString(ValueType valueType) const {
    const GenericValue& value = valueList[static_cast<int>(valueType)];
    return genericValueToString(value);
  }

  std::string RtxOptionImpl::genericValueToString(const GenericValue& value) const {
    switch (type) {
    case OptionType::Bool: return Config::generateOptionString(value.b);
    case OptionType::Int: return Config::generateOptionString(value.i);
    case OptionType::Float: return Config::generateOptionString(value.f);
    case OptionType::HashSet: return hashTableToString(*value.hashSet);
    case OptionType::HashVector: return hashVectorToString(*value.hashVector);
    case OptionType::IntVector: return vectorToString(*value.intVector);
    case OptionType::VirtualKeys: return buildKeyBindDescriptorString(*value.virtualKeys);
    case OptionType::Vector2: return Config::generateOptionString(*value.v2);
    case OptionType::Vector3: return Config::generateOptionString(*value.v3);
    case OptionType::Vector2i: return Config::generateOptionString(*value.v2i);
    case OptionType::Vector4: return Config::generateOptionString(*value.v4);
    case OptionType::String: return *value.string;
    default:
      return "unknown type";
    }
  }

  void RtxOptionImpl::readOptions(const Config& options) {
    auto& globalRtxOptions = getGlobalRtxOptionMap();
    for (auto& pPair : globalRtxOptions) {
      auto& impl = *pPair.second;
      impl.readOption(options, ValueType::Value);
    }
  }

  void RtxOptionImpl::writeOptions(Config& options, bool changedOptionsOnly) {
    auto& globalRtxOptions = getGlobalRtxOptionMap();
    for (auto& pPair : globalRtxOptions) {
      auto& impl = *pPair.second;
      impl.writeOption(options, changedOptionsOnly);
    }
  }

  void RtxOptionImpl::resetOptions() {
    auto& globalRtxOptions = getGlobalRtxOptionMap();
    for (auto& pPair : globalRtxOptions) {
      auto& impl = *pPair.second;
      impl.resetOption();
    }
  }

  void RtxOptionImpl::invokeOnChangeCallback(DxvkDevice* device) const {
    if (onChangeCallback) {
      onChangeCallback(device);
    }
  }

  bool RtxOptionImpl::clampValue(ValueType valueType) {
    GenericValue& value = valueList[static_cast<int>(valueType)];
    bool changed = false;
    
    switch (type) {
      case OptionType::Int: {
        int32_t oldValue = value.i;
        if (minValue.has_value()) {
          value.i = std::max(value.i, minValue.value().i); 
        }
        if (maxValue.has_value()) {
          value.i = std::min(value.i, maxValue.value().i); 
        }
        changed = value.i != oldValue;
        break;
      }
      case OptionType::Float: {
        float oldValue = value.f;
        if (minValue.has_value()) {
          value.f = std::max(value.f, minValue.value().f); 
        }
        if (maxValue.has_value()) {
          value.f = std::min(value.f, maxValue.value().f); 
        }
        changed = value.f != oldValue;
        break;
      }
      case OptionType::Vector2: {
        Vector2 oldValue = *value.v2;
        if (minValue.has_value()) {
          *value.v2 = max(*value.v2, *(minValue.value().v2));
        }
        if (maxValue.has_value()) {
          *value.v2 = min(*value.v2, *(maxValue.value().v2));
        }
        changed = *value.v2 != oldValue;
        break;
      }
      case OptionType::Vector3: {
        Vector3 oldValue = *value.v3;
        if (minValue.has_value()) {
          *value.v3 = max(*value.v3, *(minValue.value().v3));
        }
        if (maxValue.has_value()) {
          *value.v3 = min(*value.v3, *(maxValue.value().v3));
        }
        changed = *value.v3 != oldValue;
        break;
      }
      case OptionType::Vector2i: {
        Vector2i oldValue = *value.v2i;
        if (minValue.has_value()) {
          *value.v2i = max(*value.v2i, *(minValue.value().v2i));
        }
        if (maxValue.has_value()) {
          *value.v2i = min(*value.v2i, *(maxValue.value().v2i));
        }
        changed = *value.v2i != oldValue;
        break;
      }
      default:
        break;
    }
    return changed;
  }

  void RtxOptionImpl::readValue(const Config& options, const std::string& fullName, GenericValue& value) {
    const char* env = environment == nullptr || strlen(environment) == 0 ? nullptr : environment;

    switch (type) {
    case OptionType::Bool:
      value.b = options.getOption<bool>(fullName.c_str(), value.b, env);
      break;
    case OptionType::Int:
      value.i = options.getOption<int>(fullName.c_str(), value.i, env);
      break;
    case OptionType::Float:
      value.f = options.getOption<float>(fullName.c_str(), value.f, env);
      break;
    case OptionType::HashSet:
      fillHashTable(options.getOption<std::vector<std::string>>(fullName.c_str()), *value.hashSet);
      break;
    case OptionType::HashVector:
      fillHashVector(options.getOption<std::vector<std::string>>(fullName.c_str()), *value.hashVector);
      break;
    case OptionType::IntVector:
      fillIntVector(options.getOption<std::vector<std::string>>(fullName.c_str()), *value.intVector);
      break;
    case OptionType::VirtualKeys:
      *value.virtualKeys = options.getOption<VirtualKeys>(fullName.c_str(), *value.virtualKeys);
      break;
    case OptionType::Vector2:
      *value.v2 = options.getOption<Vector2>(fullName.c_str(), *value.v2, env);
      break;
    case OptionType::Vector3:
      *value.v3 = options.getOption<Vector3>(fullName.c_str(), *value.v3, env);
      break;
    case OptionType::Vector2i:
      *value.v2i = options.getOption<Vector2i>(fullName.c_str(), *value.v2i, env);
      break;
    case OptionType::String:
      *value.string = options.getOption<std::string>(fullName.c_str(), *value.string, env);
      break;
    case OptionType::Vector4:
      *value.v4 = options.getOption<Vector4>(fullName.c_str(), *value.v4, env);
      break;
    default:
      break;
    }
  }

  void RtxOptionImpl::readOption(const Config& options, RtxOptionImpl::ValueType valueType) {
    const std::string fullName = getFullName();
    auto& value = valueList[(int) valueType];
    readValue(options, fullName, value);

    clampValue(valueType);
    
    if (valueType == ValueType::PendingValue) {
      // If reading into the pending value, need to mark the option as dirty so it gets copied to the value at the end of the frame.
      markDirty();
    } else if (valueType == ValueType::Value) {
      // If reading into the value, need to immediately copy to the pending value so they stay in sync.
      copyValue(ValueType::Value, ValueType::PendingValue);

      // Also mark the option dirty so the onChange callback is invoked at the normal time.
      markDirty();
    }
  }

  void RtxOptionImpl::writeOption(Config& options, bool changedOptionOnly) {
    if (flags & (uint32_t)RtxOptionFlags::NoSave)
      return;
    
    std::string fullName = getFullName();
    auto& value = valueList[(int) ValueType::Value];

    if (changedOptionOnly) {
      if (isDefault()) {
        return;
      }
    }

    switch (type) {
    case OptionType::Bool:
      options.setOption(fullName.c_str(), value.b);
      break;
    case OptionType::Int:
      options.setOption(fullName.c_str(), value.i);
      break;
    case OptionType::Float:
      options.setOption(fullName.c_str(), value.f);
      break;
    case OptionType::HashSet:
      options.setOption(fullName.c_str(), hashTableToString(*value.hashSet));
      break;
    case OptionType::HashVector:
      options.setOption(fullName.c_str(), hashVectorToString(*value.hashVector));
      break;
    case OptionType::IntVector:
      options.setOption(fullName.c_str(), vectorToString(*value.intVector));
      break;
    case OptionType::VirtualKeys:
      options.setOption(fullName.c_str(), buildKeyBindDescriptorString(*value.virtualKeys));
      break;
    case OptionType::Vector2:
      options.setOption(fullName.c_str(), *value.v2);
      break;
    case OptionType::Vector3:
      options.setOption(fullName.c_str(), *value.v3);
      break;
    case OptionType::Vector2i:
      options.setOption(fullName.c_str(), *value.v2i);
      break;
    case OptionType::String:
      options.setOption(fullName.c_str(), *value.string);
      break;
    case OptionType::Vector4:
      options.setOption(fullName.c_str(), *value.v4);
      break;
    default:
      break;
    }
  }

  void RtxOptionImpl::insertOptionLayerValue(const GenericValue& value, const uint32_t priority, const float blendStrength, const float blendStrengthThreshold) {
    // Check if there's a same priority layer
    for (const auto& optionLayerValue : optionLayerValueQueue) {
      if (priority == optionLayerValue.second.priority) {
        copyValue(value, optionLayerValue.second.value);
        return;
      }
    }

    // Only float or float based vectors are allowed to mix with other option layers
    float layerBlendStrenth = blendStrength;
    if (type != OptionType::Float && type != OptionType::Vector2 && type != OptionType::Vector3 && type != OptionType::Vector4) {
      // For disallowed types, snap the strength to either 0.0f or 1.0f, based on whether it passes the strength threshold
      if (blendStrength >= blendStrengthThreshold || priority == 0) {
        layerBlendStrenth = 1.0f;
      } else {
        layerBlendStrenth = 0.0f;
      }
    }

    GenericValue optionLayerValue = createGenericValue(type);
    copyValue(value, optionLayerValue);

    const PrioritizedValue newValue { optionLayerValue, priority, layerBlendStrenth };
    auto [it, inserted] = optionLayerValueQueue.emplace(priority, newValue);
    if (!inserted) {
      Logger::warn("[RTX Option]: Duplicate priority " + std::to_string(priority) + " ignored (only first kept).");
    }
  }

  void RtxOptionImpl::readOptionLayer(const RtxOptionLayer& optionLayer) {
    GenericValueWrapper value(type);
    const std::string fullName = getFullName();
    // Only insert into queue when the option can be found in the config of option layer
    if (optionLayer.getConfig().findOption(fullName.c_str())) {
      readValue(optionLayer.getConfig(), fullName, value.data);
      insertOptionLayerValue(value.data, optionLayer.getPriority(), optionLayer.getBlendStrength(), optionLayer.getBlendStrengthThreshold());
      // When adding a new option layer, dirty current option
      markDirty();
    } else {
      Logger::warn("[RTX Option] Attempted to read option that is not defined in the option layer.");
    }
  }

  void RtxOptionImpl::disableLayerValue(const uint32_t priority) {
    auto it = optionLayerValueQueue.find(priority);
    if (it != optionLayerValueQueue.end()) {
      // When removing a layer, dirty current option
      markDirty();
      optionLayerValueQueue.erase(it);
    }
  }

  void RtxOptionImpl::disableTopLayer() {
    if (!optionLayerValueQueue.empty()) {
      optionLayerValueQueue.erase(optionLayerValueQueue.begin());
    }
  }

  void RtxOptionImpl::updateLayerBlendStrength(const RtxOptionLayer& optionLayer) {
    const std::string fullName = getFullName();
    // Only update the strength when the option can be found in the config of option layer
    if (optionLayer.getConfig().findOption(fullName.c_str())) {
      // Find the option layer value that the strength needs to be updated (same priority)
      for (auto& optionLayerValue : optionLayerValueQueue) {
        if (optionLayer.getPriority() == optionLayerValue.second.priority) {
          if (type == OptionType::Float || type == OptionType::Vector2 || type == OptionType::Vector3 || type == OptionType::Vector4) {
            // Only float or float based vectors are allowed to mix with other option layers
            optionLayerValue.second.blendStrength = optionLayer.getBlendStrength();
          } else {
            // For disallowed types, snap the strength to either 0.0f or 1.0f, based on whether it passes the strength threshold
            if (optionLayer.getBlendStrength() >= optionLayer.getBlendStrengthThreshold()) {
              optionLayerValue.second.blendStrength = 1.0f;
            } else {
              optionLayerValue.second.blendStrength = 0.0f;
            }
          }
          return;
        }
      }
    } else {
      Logger::warn("[RTX Option] Attempted to update option that is not defined in the option layer.");
    }
  }

  bool RtxOptionImpl::isDefault() const {
    return isEqual(ValueType::Value, ValueType::DefaultValue);
  }

  bool RtxOptionImpl::isEqual(const GenericValue& aValue, const GenericValue& bValue) const {
    switch (type) {
    case OptionType::Bool:
      return aValue.b == bValue.b;
      break;
    case OptionType::Int:
      return aValue.i == bValue.i;
      break;
    case OptionType::Float:
      return aValue.f == bValue.f;
      break;
    case OptionType::HashSet:
      return *aValue.hashSet == *bValue.hashSet;
      break;
    case OptionType::HashVector:
      return *aValue.hashVector == *bValue.hashVector;
      break;
    case OptionType::IntVector:
      return *aValue.intVector == *bValue.intVector;
      break;
    case OptionType::VirtualKeys:
      return *aValue.virtualKeys == *bValue.virtualKeys;
      break;
    case OptionType::Vector2:
      return *aValue.v2 == *bValue.v2;
      break;
    case OptionType::Vector3:
      return *aValue.v3 == *bValue.v3;
      break;
    case OptionType::Vector2i:
      return *aValue.v2i == *bValue.v2i;
      break;
    case OptionType::String:
      return *aValue.string == *bValue.string;
      break;
    case OptionType::Vector4:
      return *aValue.v4 == *bValue.v4;
      break;
    }
    return false;
  }

  bool RtxOptionImpl::isEqual(ValueType a, ValueType b) const {
    return isEqual(valueList[(int) a], valueList[(int) b]);
  }

  void RtxOptionImpl::resetOption() {
    if (flags & (uint32_t) RtxOptionFlags::NoReset)
      return;
    
    // If value and defaultValue are equal, no need to to change the Value.
    if (isEqual(ValueType::Value, ValueType::DefaultValue)) {
      // Check if the option has a pending value, and if so reset that.
      if (isEqual(ValueType::PendingValue, ValueType::DefaultValue)) {
        copyValue(ValueType::DefaultValue, ValueType::PendingValue);
      }
      return;
    }

    copyValue(ValueType::DefaultValue, ValueType::PendingValue);
    markDirty();
  }

  void RtxOptionImpl::copyValue(const GenericValue& source, GenericValue& target) {
    switch (type) {
    case OptionType::Bool:
      target.b = source.b;
      break;
    case OptionType::Int:
      target.i = source.i;
      break;
    case OptionType::Float:
      target.f = source.f;
      break;
    case OptionType::HashSet:
      *target.hashSet = *source.hashSet;
      break;
    case OptionType::HashVector:
      *target.hashVector = *source.hashVector;
      break;
    case OptionType::IntVector:
      *target.intVector = *source.intVector;
      break;
    case OptionType::VirtualKeys:
      *target.virtualKeys = *source.virtualKeys;
      break;
    case OptionType::Vector2:
      *target.v2 = *source.v2;
      break;
    case OptionType::Vector3:
      *target.v3 = *source.v3;
      break;
    case OptionType::Vector2i:
      *target.v2i = *source.v2i;
      break;
    case OptionType::String:
      *target.string = *source.string;
      break;
    case OptionType::Vector4:
      *target.v4 = *source.v4;
      break;
    default:
      break;
    }
  }

  void RtxOptionImpl::copyValue(ValueType source, ValueType target) {
    copyValue(valueList[(int) source], valueList[(int) target]);
  }

  void RtxOptionImpl::addWeightedValue(const GenericValue& source, const float weight, GenericValue& target) {
    switch (type) {
    case OptionType::Float:
      target.f += source.f * weight;
      break;
    case OptionType::Vector2:
      *target.v2 += *source.v2 * weight;
      break;
    case OptionType::Vector3:
      *target.v3 += *source.v3 * weight;
      break;
    case OptionType::Vector4:
      *target.v4 += *source.v4 * weight;
      break;
    case OptionType::Bool:
    case OptionType::Int:
    case OptionType::HashSet:
    case OptionType::HashVector:
    case OptionType::IntVector:
    case OptionType::VirtualKeys:
    case OptionType::Vector2i:
    case OptionType::String:
      target = source;
      break;
    default:
      break;
    }
  }

  void RtxOptionImpl::copyValueToOptionLayer(ValueType source) {
    const auto& sourceValue = valueList[(int) source];
    // Ensure the top-most layer is the runtime layer, insert if missing.
    if (optionLayerValueQueue.begin()->second.priority != RtxOptionLayer::s_runtimeOptionLayerPriority) {
      // Insert runtime layer with full strength so runtime overrides win immediately.
      insertOptionLayerValue(sourceValue, RtxOptionLayer::s_runtimeOptionLayerPriority, 1.0f, 1.0f);
    } else {
      // Runtime layer already present at top, update its value in-place.
      copyValue(sourceValue, optionLayerValueQueue.begin()->second.value);
    }
  }

  void RtxOptionImpl::copyOptionLayerToValue() {
    GenericValue& optionValueTop = optionLayerValueQueue.begin()->second.value;
    const auto& pendingValue = valueList[(int) RtxOptionImpl::ValueType::PendingValue];

    if (optionLayerValueQueue.begin()->second.priority == RtxOptionLayer::s_runtimeOptionLayerPriority &&
        !isEqual(pendingValue, optionValueTop)) {
      // Sync the values that are changed at real-time to top option layer
      copyValue(pendingValue, optionValueTop);
#if RTX_OPTION_DEBUG_LOGGING
      Logger::info(str::format("[RTX Option]: Different to pending option ", this->name,
                               "\ntype: ", std::to_string((int) this->type),
                               "\nbool: ", std::to_string(pendingValue.b), " ", std::to_string(optionValueTop.b),
                               "\nint: ", std::to_string(pendingValue.i), " ", std::to_string(optionValueTop.i),
                               "\nfloat: ", std::to_string(pendingValue.f), " ", std::to_string(optionValueTop.f)
      ));
#endif
    }

    /*
      We use "throughput" here because blending (lerp) may happen across multiple layers.
      The effective result is a nested lerp chain, e.g.: v = lerp(A, lerp(B, C))

      Since we process layers from highest priority to lowest, we need throughput
      to track how much weight remains for lower-priority layers.

      Example (layers in priority order with blend strengths):
          A: 0.2
          B: 0.5
          C: 1.0
          D: 1.0

      The naive evaluation is: v = lerp( lerp( lerp(D, C, 1.0), B, 0.5 ), A, 0.2 )

      Because C has strength 1.0: lerp(D, C, 1.0) == C

      So this simplifies to: v = lerp( lerp(C, B, 0.5), A, 0.2 )

      Throughput ensures each layer's contribution is scaled correctly, and we can early-exit once a layer has blendStrength == 1.0
      (since lower-priority layers won't affect the result).
    */
    GenericValueWrapper optionValue(type);
    float throughput = 1.0f;
    // Loop layers from highest priority to lowest to lerp the value across layers base on the blend strength of layers
    for (const auto& optionLayer : optionLayerValueQueue) {
      // Stop when the blend strength is larger than 1, because lerp(a, b, 1.0f) => b, we don't need to loop lower priority values
      if (optionLayer.second.blendStrength >= 1.0f) {
        addWeightedValue(optionLayer.second.value, throughput, optionValue.data);
        break;
      }

      addWeightedValue(optionLayer.second.value, optionLayer.second.blendStrength * throughput, optionValue.data);
      throughput *= (1.0f - optionLayer.second.blendStrength);
    }

    // Copy to valueList
    copyValue(optionValue.data, valueList[(int) RtxOptionImpl::ValueType::Value]);
  }

  bool RtxOptionImpl::writeMarkdownDocumentation(const char* outputMarkdownFilePath) {
    // Open the output file for writing
    std::ofstream outputFile(outputMarkdownFilePath);
    if (!outputFile.is_open()) {
      Logger::err(str::format("[RTX Option]: Failed to open output file ", outputMarkdownFilePath, " for writing"));
      return false;
    }

    // Write out the header for the file
    outputFile <<
      "# RTX Options\n";

    // Add description of Rtx Options
    outputFile <<
R"(RTX Options are configurable parameters for RTX pipeline components. They can be set via rtx.conf in a following format:

```
<RTX Option int scalar> = <Integer value>
<RTX Option float scalar> = <Floating point value>
<RTX Option int vector> = <Integer value>, <Integer value>, ...
<RTX Option float vector> = <Floating point value>, <Floating point value>, ...
<RTX Option boolean> = True/False
<RTX Option string> = <String value, no quotes>
<RTX Option hash set/vector> = <Hex string>, <Hex string>, ...
```

Practical examples of syntax:

```
rtx.someIntScalar = 38
rtx.someFloatScalar = 29.39
rtx.someIntVector = 1, -2, 3
rtx.someFloatVector = 1.0, -2.0, 3.0
rtx.someBoolean = True
# Note: Leading whitespace in a string is removed, allowing for nicer option formatting like this without messing up the string.
# Additionally, strings should not be surrounded with quotes as these will be treated as part of the string.
rtx.someString = This is a string
# Note: 0x prefix on hash hex values here is optional, similarly these values are case-insensitive. 16 hex characters = 64 bit hash.
rtx.someHashSet = 8DD6F568BD126398, EEF8EFD4B8A1B2A5, ...
```

RTX Options may be set in multiple places, specifically a hardcoded set in `src/util/config/config.cpp` which is assigned per-application based on process name, and the two user-configurable files `dxvk.conf` and `rtx.conf`. If not set the options will inherit their default values.
The full order of precedence for how each set of options overrides the previous is as follows:

1. Default option value (Implicit)
2. `dxvk.conf` ("User Config")
3. Per-application `config.cpp` configuration ("Built-in Config")
4. `rtx.conf` ("RTX User Config")
  1. `baseGameModPath/rtx.conf` (Mod-specific extension of "RTX User Config")

Additionally, upon saving options from the Remix UI options are written only to rtx.conf.

Tables below enumerate all the options and their defaults set by RTX Remix. Note that this information is auto-generated by the RTX Remix application. To re-generate this file, run Remix with `DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD=1` defined in the environment variables.

)";

    // Helper function to write out a table of RtxOptions for a given value type category
    auto writeOutRtxOptionTable = [&](bool processLongEntryTypes) {
      // Write out a header for a Markdown table
      outputFile << 
        "| RTX Option | Type | Default Value | Min Value | Max Value | Description |\n"
        "| :-- | :-: | :-: | :-: | :-: | :-- |\n"; // Text alignment per column

      // Write out all RTX Options
      auto& globalRtxOptions = getGlobalRtxOptionMap();

      // Need to sort the options alphabetically by full name.
      std::vector<RtxOptionImpl*> sortedOptions;
      sortedOptions.reserve(globalRtxOptions.size());
      for (const auto& rtxOptionMapEntry : globalRtxOptions) {
        sortedOptions.push_back(rtxOptionMapEntry.second.get());
      }  
      std::sort(sortedOptions.begin(), sortedOptions.end(), [](RtxOptionImpl* a, RtxOptionImpl* b) {
        return a->getFullName() < b->getFullName();
      });

      for (const RtxOptionImpl* rtxOptionsPtr : sortedOptions) {
        const RtxOptionImpl& rtxOption = *rtxOptionsPtr;

        // Allow processing of short or long value entry categories separately
        {
          bool isLongEntryType = false;

          switch (rtxOption.type) {
          case OptionType::HashSet:
          case OptionType::HashVector:
          case OptionType::IntVector:
          case OptionType::VirtualKeys:
          case OptionType::String:
            isLongEntryType = true;
            break;
          default:
            break;
          }

          if (isLongEntryType != processLongEntryTypes)
            continue;
        }

        std::string defaultValueString = rtxOption.genericValueToString(ValueType::DefaultValue);
        std::string minValueString = rtxOption.minValue.has_value() ? rtxOption.genericValueToString(*rtxOption.minValue) : "";
        std::string maxValueString = rtxOption.maxValue.has_value() ? rtxOption.genericValueToString(*rtxOption.maxValue) : "";

        // Write the first portion of the result row to the the outputstream
        outputFile <<
          "|" << rtxOption.getFullName() <<
          "|" << rtxOption.getTypeString() <<
          "|" << defaultValueString <<
          "|" << minValueString <<
          "|" << maxValueString <<
          "|";

        // Preprocess option description for Markdown
        // Note: This is needed as often in our description strings we use various characters which need to be handled
        // when translated to markdown to avoid breaking formatting.

        for (const char* descriptionIterator = rtxOption.description; ; ++descriptionIterator) {
          // Todo: Proper UTF-8 handling here if we ever need that. Not too hard to detect UTF-8 codepoints
          // (see: https://stackoverflow.com/a/40054802 for example), just would need to do this if option strings
          // actually start having anything beyond ASCII in them (otherwise this logic will break down and may
          // corrupt the displayed text).
          const auto currentCharacter = *descriptionIterator;

          if (currentCharacter == '\0') {
            break;
          }

          switch (currentCharacter) {
          // Note: Escape < and > as these act as HTML tags in various contexts.
          case '<': outputFile << "\\<"; break;
          case '>': outputFile << "\\>"; break;
          // Note: Convert newlines to HTML line breaks.
          case '\n': outputFile << "<br>"; break;
          // Note: Escape general Markdown syntax characters (this disallows usage of Markdown in description strings which
          // may be undesirable at some point, but for now our description strings are authored in a way that is shared
          // between the UI as well so they are usually not authored with expectation of Markdown syntax to function).
          case '\\': outputFile << "\\\\"; break;
          case '`': outputFile << "\\`"; break;
          case '*': outputFile << "\\*"; break;
          case '_': outputFile << "\\_"; break;
          case '{': outputFile << "\\{"; break;
          case '}': outputFile << "\\}"; break;
          case '[': outputFile << "\\["; break;
          case ']': outputFile << "\\]"; break;
          case '(': outputFile << "\\("; break;
          case ')': outputFile << "\\)"; break;
          case '#': outputFile << "\\#"; break;
          case '+': outputFile << "\\+"; break;
          case '-': outputFile << "\\-"; break;
          case '.': outputFile << "\\."; break;
          case '!': outputFile << "\\!"; break;
          // Note: Non-standard Markdown, but escaping it this way should work probably (if not then switch to using a HTML entity).
          case '|': outputFile << "\\|"; break;
          default:
            outputFile.put(currentCharacter);

            break;
          }
        }

        // Write the final portion of the result row to the the outputstream
        outputFile << "|\n";
      }
    };

    // Split short and long entry value types into two tables to improve readability for short value types
    // Long entry value types can be very long and drag out the width for the default value column
    outputFile << "## Simple Types\n";
    writeOutRtxOptionTable(false);

    outputFile << std::endl;

    outputFile << "## Complex Types\n";
    writeOutRtxOptionTable(true);

    outputFile.close();

    return true;
  }

  RtxOptionImpl::RtxOptionMap& RtxOptionImpl::getGlobalRtxOptionMap() {
    // Since other static RtxOptions may try to access the global container on their intialization, 
    // they have to access it via this helper method and the global container has to be defined 
    // as static locally to ensure it is initialized on first use
    static RtxOptionMap s_rtxOptions = RtxOptionMap();
    return s_rtxOptions;
  }

  fast_unordered_cache<RtxOptionImpl*>& RtxOptionImpl::getDirtyRtxOptionMap() {
    // Since other static RtxOptions may try to access the global container on their intialization, 
    // they have to access it via this helper method and the global container has to be defined 
    // as static locally to ensure it is initialized on first use
    static fast_unordered_cache<RtxOptionImpl*> s_dirtyOptions = fast_unordered_cache<RtxOptionImpl*>();
    return s_dirtyOptions;
  }

  RtxOptionImpl::RtxOptionLayerMap& RtxOptionImpl::getRtxOptionLayerMap() {
    static RtxOptionLayerMap s_rtxOptionLayers = RtxOptionLayerMap();
    return s_rtxOptionLayers;
  }

  bool writeMarkdownDocumentation(const char* outputMarkdownFilePath) {
    return dxvk::RtxOptionImpl::writeMarkdownDocumentation(outputMarkdownFilePath);
  }

  // Option Layer
  bool RtxOptionLayer::s_resetRuntimeSettings = false;

  RtxOptionLayer::RtxOptionLayer(const std::string& configPath, const uint32_t priority, const float blendStrength, const float blendThreshold)
    : m_configName(configPath)
    , m_enabled(true)
    , m_dirty(false)
    , m_priority(priority + s_userOptionLayerOffset)
    , m_blendStrength(blendStrength)
    , m_blendThreshold(blendThreshold)
    , m_config(Config::getOptionLayerConfig(configPath)) {
#if RTX_OPTION_DEBUG_LOGGING
    Logger::info(str::format("[RTX Option]: Added user option layer: ", m_configName,
                             "\nPriority: ", std::to_string(m_priority),
                             "\nStrength: ", std::to_string(m_blendStrength)));
#endif
  }

  RtxOptionLayer::RtxOptionLayer(const Config& config, const std::string& configName, const uint32_t priority, const float blendStrength, const float blendThreshold)
    : m_configName(configName)
    , m_enabled(true)
    , m_dirty(false)
    , m_config(config)
    , m_priority(priority)
    , m_blendStrength(blendStrength)
    , m_blendThreshold(blendThreshold) {
#if RTX_OPTION_DEBUG_LOGGING
    Logger::info(str::format("[RTX Option]: Added app option layer: ", m_configName,
                             "\nPriority: ", std::to_string(m_priority),
                             "\nStrength: ", std::to_string(m_blendStrength)));
#endif
  }

  RtxOptionLayer::~RtxOptionLayer() = default;

}

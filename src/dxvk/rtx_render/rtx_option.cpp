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

    releaseValue(resolvedValue, type);
  }

  const GenericValue& RtxOptionImpl::getGenericValue(const ValueType valueType) const {
    static const GenericValue dummyValue {};
    if (valueType == ValueType::DefaultValue) {
      if (optionLayerValueQueue.size() == 0) {
        Logger::err("Empty option layer queue. The default value of option: " + std::string(name) + " is NOT properly set.");
        return dummyValue;
      }
      // Return the lowest priority value (last element in the map ordered by descending priority)
      return optionLayerValueQueue.rbegin()->second.value;
    } else if (valueType == ValueType::PendingValue) {
      if (optionLayerValueQueue.size() > 0 && optionLayerValueQueue.begin()->first.priority != RtxOptionLayer::s_runtimeOptionLayerPriority) {
        Logger::err("Failed to get runtime layer. The pending value of option: " + std::string(name) + " is missing.");
        return dummyValue;
      }
      return optionLayerValueQueue.begin()->second.value;
    } else if (valueType == ValueType::Value) {
      return resolvedValue;
    } else {
      Logger::warn("[RTX Option]: Unknown generic value type.");
      return dummyValue;
    }
  }

  GenericValue& RtxOptionImpl::getGenericValue(const ValueType valueType) {
    // Insert runtime layer if it's missing and user request runtime changes.
    if (optionLayerValueQueue.size() > 0 && optionLayerValueQueue.begin()->first.priority != RtxOptionLayer::s_runtimeOptionLayerPriority) {
      const RtxOptionLayer* runtimeLayer = getRuntimeLayer();
      if (runtimeLayer) {
        insertOptionLayerValue(optionLayerValueQueue.begin()->second.value, runtimeLayer);
      }
    }

    // Reuse the const overload to avoid duplicating switch logic.
    // const_cast is safe here because we are returning a non-const reference
    // only when called on a non-const RtxOptionImpl instance.
    return const_cast<GenericValue&>(static_cast<const RtxOptionImpl*>(this)->getGenericValue(valueType));
  }

  const char* RtxOptionImpl::getTypeString() const {
    switch (type) {
    case OptionType::Bool: return "bool";
    case OptionType::Int: return "int";
    case OptionType::Float: return "float";
    case OptionType::HashSet: return "hash set"; 
    case OptionType::HashVector: return "hash vector";
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
    const auto& value = getGenericValue(valueType);
    return genericValueToString(value);
  }

  std::string RtxOptionImpl::genericValueToString(const GenericValue& value) const {
    switch (type) {
    case OptionType::Bool: return Config::generateOptionString(value.b);
    case OptionType::Int: return Config::generateOptionString(value.i);
    case OptionType::Float: return Config::generateOptionString(value.f);
    case OptionType::HashSet: return hashTableToString(*value.hashSet);
    case OptionType::HashVector: return hashVectorToString(*value.hashVector);
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

  bool RtxOptionImpl::clampValue(GenericValue& value) {
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

  bool RtxOptionImpl::clampValue(ValueType valueType) {
    auto& value = const_cast<GenericValue&>(getGenericValue(valueType));
    return clampValue(value);
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
    auto& value = getGenericValue(valueType);
    readValue(options, fullName, value);

    if (valueType == ValueType::PendingValue) {
      // If reading into the pending value, need to mark the option as dirty so it gets resolved to the value at the end of the frame.
      markDirty();
    } else if (valueType == ValueType::Value) {
      // If reading into the value, need to immediately copy to the pending value so they stay in sync.
      copyValue(resolvedValue, getGenericValue(ValueType::PendingValue));

      // Also mark the option dirty so the onChange callback is invoked at the normal time.
      markDirty();
    }
  }

  void RtxOptionImpl::writeOption(Config& options, bool changedOptionOnly) {
    if (flags & (uint32_t)RtxOptionFlags::NoSave)
      return;
    
    std::string fullName = getFullName();
    auto& value = resolvedValue;

    if (changedOptionOnly) {
      // Skip options that have no real-time changes, or the real-time value is the same as the original resolved value.
      if (optionLayerValueQueue.begin()->first.priority != RtxOptionLayer::s_runtimeOptionLayerPriority) {
        return;
      } else {
        GenericValueWrapper originalValue(type);
        resolveValue(originalValue.data, true);

        if (isEqual(originalValue.data, getGenericValue(ValueType::PendingValue))) {
          return;
        }
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

  void RtxOptionImpl::insertEmptyOptionLayer(const RtxOptionLayer* layer) {
    GenericValue optionLayerValue = createGenericValue(type);
    const PrioritizedValue newValue(optionLayerValue, layer->getBlendStrength(), layer->getBlendStrengthThreshold());
    
    LayerKey key = {layer->getPriority(), layer->getName()};
    auto [it, inserted] = optionLayerValueQueue.emplace(key, newValue);
    if (!inserted) {
      Logger::warn("[RTX Option]: Duplicate layer '" + std::string(layer->getName()) + "' with priority " + std::to_string(layer->getPriority()) + " ignored (only first kept).");
    }
  }

  void RtxOptionImpl::insertOptionLayerValue(const GenericValue& value, const RtxOptionLayer* layer) {
    if (layer == nullptr) {
      Logger::warn("[RTX Option]: Cannot insert layer value with null layer pointer.");
      return;
    }
    
    LayerKey key = {layer->getPriority(), layer->getName()};
    
    // Check if this exact layer already exists
    auto existingIt = optionLayerValueQueue.find(key);
    if (existingIt != optionLayerValueQueue.end()) {
      // Update existing value
      copyValue(value, existingIt->second.value);
      return;
    }

    // Create new value and copy from source
    GenericValue optionLayerValue = createGenericValue(type);
    copyValue(value, optionLayerValue);

    const PrioritizedValue newValue(optionLayerValue, layer->getBlendStrength(), layer->getBlendStrengthThreshold());
    auto [it, inserted] = optionLayerValueQueue.emplace(key, newValue);
    if (!inserted) {
      Logger::warn("[RTX Option]: Duplicate layer '" + std::string(layer->getName()) + "' with priority " + std::to_string(layer->getPriority()) + " ignored (only first kept).");
    }
  }

  void RtxOptionImpl::readOptionLayer(const RtxOptionLayer& optionLayer) {
    GenericValueWrapper value(type);
    const std::string fullName = getFullName();
    // Only insert into queue when the option can be found in the config of option layer
    if (optionLayer.getConfig().findOption(fullName.c_str())) {
      readValue(optionLayer.getConfig(), fullName, value.data);
      // All layer properties (priority, blend strength, threshold) are read from the layer itself
      insertOptionLayerValue(value.data, &optionLayer);
      // When adding a new option layer, dirty current option
      markDirty();
    }
  }

  void RtxOptionImpl::disableLayerValue(const RtxOptionLayer* layer) {
    if (layer == nullptr) {
      return;
    }
    
    LayerKey key = {layer->getPriority(), layer->getName()};
    auto it = optionLayerValueQueue.find(key);
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
      // Find the option layer value by exact layer match
      LayerKey key = {optionLayer.getPriority(), optionLayer.getName()};
      auto optionLayerIter = optionLayerValueQueue.find(key);
      if (optionLayerIter != optionLayerValueQueue.end()) {
        optionLayerIter->second.blendStrength = optionLayer.getBlendStrength();
        optionLayerIter->second.blendThreshold = optionLayer.getBlendStrengthThreshold();
      }
    }
  }

  bool RtxOptionImpl::isDefault() const {
    return isEqual(resolvedValue, getGenericValue(ValueType::DefaultValue));
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

  void RtxOptionImpl::resetOption() {
    if (flags & (uint32_t) RtxOptionFlags::NoReset)
      return;
    
    // If value and defaultValue are equal, no need to to change the Value.
    if (isEqual(resolvedValue, getGenericValue(ValueType::DefaultValue))) {
      // Check if the option has a pending value, and if so reset that.
      if (isEqual(getGenericValue(ValueType::PendingValue), getGenericValue(ValueType::DefaultValue))) {
        copyValue(getGenericValue(ValueType::DefaultValue), getGenericValue(ValueType::PendingValue));
      }
      return;
    }

    copyValue(getGenericValue(ValueType::DefaultValue), getGenericValue(ValueType::PendingValue));
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
    case OptionType::HashSet:
    {
      target.hashSet->insert(source.hashSet->begin(), source.hashSet->end());
      break;
    }
    case OptionType::Bool:
    case OptionType::Int:
    case OptionType::VirtualKeys:
    case OptionType::Vector2i:
    case OptionType::String:
    case OptionType::HashVector: // Hash Vectors are strictly ordered and can be size bounded, so we don't want to merge them.
      target = source;
      break;
    default:
      break;
    }
  }

  bool RtxOptionImpl::resolveValue(GenericValue& value, const bool ignoreChangedOption) {
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
    bool layerMatchingRuntimePriorityFound = false;
    // Loop layers from highest priority to lowest to lerp the value across layers base on the blend strength of layers
    for (const auto& optionLayer : optionLayerValueQueue) {
      if (optionLayer.first.priority == RtxOptionLayer::s_runtimeOptionLayerPriority) {
        if (ignoreChangedOption) {
          // Skip options with runtime priority when ignoreChangedOption is true
          continue;
        }

        // Changing this flag must happen after checking ignoreChangedOption, or the real-time changes will be mistakenly removed.
        layerMatchingRuntimePriorityFound = true;
      }

      if (type == OptionType::Float || type == OptionType::Vector2 || type == OptionType::Vector3 || type == OptionType::Vector4) {
        // Stop when the blend strength is larger than 1, because lerp(a, b, 1.0f) => b, we don't need to loop lower priority values
        if (optionLayer.second.blendStrength >= 1.0f) {
          addWeightedValue(optionLayer.second.value, throughput, optionValue.data);
          break;
        }

        addWeightedValue(optionLayer.second.value, optionLayer.second.blendStrength * throughput, optionValue.data);
        throughput *= (1.0f - optionLayer.second.blendStrength);

        if (throughput < 0.0001f) {
          break;
        }
      } else {
        if (optionLayer.second.blendStrength < optionLayer.second.blendThreshold) {
          continue;
        }
        addWeightedValue(optionLayer.second.value, throughput, optionValue.data);
        // For non-set types, we always break after applying the weight
        if (type != OptionType::HashSet) {
          break;
        }
        
      }
    }

    // Clamp the resolved value. There is no need to check if the clamp changed the value because we are already in the middle of changing the value
    clampValue(optionValue.data);

    // If a runtime option layer exists, recompute the resolved value without it
    // to check whether the layer actually changes the final result. If the recomputed value
    // matches the current resolved value, it means the real-time layer is redundant,
    // so we remove (disable) it to avoid unnecessary layers and redundant blending.
    if (layerMatchingRuntimePriorityFound) {
      GenericValueWrapper originalResolvedValue(type);
      for (const auto& optionLayer : optionLayerValueQueue) {
        if (optionLayer.first.priority == RtxOptionLayer::s_runtimeOptionLayerPriority) {
          continue;
        }

        if (type == OptionType::Float || type == OptionType::Vector2 || type == OptionType::Vector3 || type == OptionType::Vector4) {
          // Stop when the blend strength is larger than 1, because lerp(a, b, 1.0f) => b, we don't need to loop lower priority values
          if (optionLayer.second.blendStrength >= 1.0f) {
            addWeightedValue(optionLayer.second.value, throughput, originalResolvedValue.data);
            break;
          }

          addWeightedValue(optionLayer.second.value, optionLayer.second.blendStrength * throughput, originalResolvedValue.data);
          throughput *= (1.0f - optionLayer.second.blendStrength);
        } else {
          if (optionLayer.second.blendStrength >= optionLayer.second.blendThreshold || optionLayer.first.priority == 0) {
            addWeightedValue(optionLayer.second.value, throughput, originalResolvedValue.data);
            break;
          }
        }
      }

      clampValue(originalResolvedValue.data);

      if (isEqual(originalResolvedValue.data, optionValue.data)) {
        disableTopLayer();
      }
    }

    const bool valueHasChanged = !isEqual(optionValue.data, value);
    if (valueHasChanged) {
      // Copy to resolvedValue
      copyValue(optionValue.data, value);
    }
    return valueHasChanged;
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

  RtxOptionLayer* RtxOptionImpl::getRtxOptionLayer(const uint32_t priority, const std::string_view configName) {
    LayerKey key = {priority, configName};
    auto& layerMap = getRtxOptionLayerMap();
    auto it = layerMap.find(key);
    if (it != layerMap.end()) {
      return it->second.get();
    }
    return nullptr;
  }

  const RtxOptionLayer* RtxOptionImpl::addRtxOptionLayer(
    const std::string& configPath, const uint32_t priority, const bool isSystemOptionLayer,
    const float blendStrength, const float blendThreshold, const Config* config) {
    // Adjust rtx.conf path if env var DXVK_RTX_CONFIG_FILE is set
    const std::string adjustedConfigPath = configPath == "rtx.conf" ? RtxOptions::getRtxConfPath() : configPath;

    // Load config from path if not provided
    const Config& layerConfig = config ? *config : Config::getOptionLayerConfig(adjustedConfigPath);
    
    // Clamp priority to valid range
    // System layers can use 0-99, user layers 100+, runtime layer uses max value
    uint32_t clampedPriority = priority;
    if (!isSystemOptionLayer) {
      // User layers: clamp to [s_userOptionLayerOffset, s_runtimeOptionLayerPriority-1]
      if (priority < RtxOptionLayer::s_userOptionLayerOffset && priority != RtxOptionLayer::s_runtimeOptionLayerPriority) {
        clampedPriority = RtxOptionLayer::s_userOptionLayerOffset;
        Logger::warn(str::format("[RTX Option]: Priority ", priority, " for '", configPath, "' is below minimum. Clamping to ", clampedPriority, "."));
      }
    } else if (priority != RtxOptionLayer::s_runtimeOptionLayerPriority) {
      // System layers: clamp to [0, s_userOptionLayerOffset-1]
      if (priority >= RtxOptionLayer::s_userOptionLayerOffset) {
        clampedPriority = RtxOptionLayer::s_userOptionLayerOffset - 1;
        Logger::warn(str::format("[RTX Option]: Priority ", priority, " for '", configPath, "' is above maximum for system layers. Clamping to ", clampedPriority, "."));
      }
    }

    // Create the layer first
    auto layer = std::make_unique<RtxOptionLayer>(layerConfig, configPath, clampedPriority, blendStrength, blendThreshold);
    
    // Check if the newly constructed layer is valid
    if (!layer->isValid()) {
      Logger::warn(str::format("[RTX Option]: Failed to load valid config for layer '", adjustedConfigPath, "' with priority ", clampedPriority, "."));
      return nullptr;
    }
    
    auto& layerMap = getRtxOptionLayerMap();
    
    // Now create key using string_view to the layer's owned name
    LayerKey layerKey = {clampedPriority, layer->getName()};
    
    // Insert into map
    auto [it, inserted] = layerMap.emplace(layerKey, std::move(layer));
    
    if (!inserted) {
      // Layer with this (priority, config) combination already exists
      Logger::warn(str::format("[RTX Option]: Layer '", configPath, "' with priority ", clampedPriority, " already exists."));
      return nullptr;
    }
    
    return it->second.get();
  }

  bool RtxOptionImpl::removeRtxOptionLayer(const RtxOptionLayer* layer) {
    if (layer == nullptr) {
      return false;
    }

    auto& layerMap = getRtxOptionLayerMap();
    LayerKey layerKey = {layer->getPriority(), layer->getName()};
    auto it = layerMap.find(layerKey);
    
    if (it != layerMap.end()) {
      // Remove the layer values from all RtxOptions
      auto& globalRtxOptions = getGlobalRtxOptionMap();
      for (auto& rtxOptionMapEntry : globalRtxOptions) {
        RtxOptionImpl& rtxOption = *rtxOptionMapEntry.second.get();
        rtxOption.disableLayerValue(layer);
      }
      
      // Remove from the global layer map
      layerMap.erase(it);
      return true;
    }
    
    return false;
  }

  const RtxOptionLayer* RtxOptionImpl::getRuntimeLayer() {
    auto& layerMap = getRtxOptionLayerMap();
    LayerKey layerKey = {RtxOptionLayer::s_runtimeOptionLayerPriority, "user.conf"};
    auto it = layerMap.find(layerKey);
    
    if (it != layerMap.end()) {
      return it->second.get();
    }
    
    // Create runtime layer with empty config - does not load any config file
    Config emptyConfig;
    auto layer = std::make_unique<RtxOptionLayer>(emptyConfig, "user.conf", RtxOptionLayer::s_runtimeOptionLayerPriority, 1.0f, 0.1f);
    
    // Insert into map
    auto [insertIt, inserted] = layerMap.emplace(layerKey, std::move(layer));
    
    if (!inserted) {
      Logger::warn("[RTX Option]: Failed to create runtime layer (unexpected insertion failure).");
      return nullptr;
    }
    
    return insertIt->second.get();
  }

  const RtxOptionLayer* RtxOptionImpl::getDefaultLayer() {
    auto& layerMap = getRtxOptionLayerMap();
    LayerKey layerKey = {(uint32_t)RtxOptionLayer::SystemLayerPriority::Default, "default"};
    auto it = layerMap.find(layerKey);
    
    if (it != layerMap.end()) {
      return it->second.get();
    }
    
    // Create default layer with empty config - does not load any config file, holds in-code default values
    Config emptyConfig;
    auto layer = std::make_unique<RtxOptionLayer>(emptyConfig, "default", (uint32_t)RtxOptionLayer::SystemLayerPriority::Default, 1.0f, 0.1f);
    
    // Insert into map
    auto [insertIt, inserted] = layerMap.emplace(layerKey, std::move(layer));
    
    if (!inserted) {
      Logger::warn("[RTX Option]: Failed to create default layer (unexpected insertion failure).");
      return nullptr;
    }
    
    return insertIt->second.get();
  }

  bool writeMarkdownDocumentation(const char* outputMarkdownFilePath) {
    return dxvk::RtxOptionImpl::writeMarkdownDocumentation(outputMarkdownFilePath);
  }

  // Option Layer
  bool RtxOptionLayer::s_resetRuntimeSettings = false;

  RtxOptionLayer::RtxOptionLayer(const Config& config, const std::string& configName, const uint32_t priority, const float blendStrength, const float blendThreshold)
    : m_configName(configName)
    , m_enabled(true)
    , m_dirty(false)
    , m_config(config)
    , m_priority(priority)
    , m_blendStrength(blendStrength)
    , m_blendThreshold(blendThreshold)
    , m_pendingEnabledRequest(EnabledRequest::NoRequest)
    , m_pendingMaxBlendStrength(kEmptyBlendStrengthRequest)
    , m_pendingMinBlendThreshold(kEmptyBlendThresholdRequest) {
#if RTX_OPTION_DEBUG_LOGGING
    Logger::info(str::format("[RTX Option]: Added option layer: ", m_configName,
                             "\nPriority: ", std::to_string(m_priority),
                             "\nStrength: ", std::to_string(m_blendStrength)));
#endif
  }

  RtxOptionLayer::~RtxOptionLayer() = default;

  void RtxOptionLayer::resolvePendingRequests() {
    // Resolve enabled state if any component made a request
    if (m_pendingEnabledRequest != EnabledRequest::NoRequest) {
      bool newEnabledState = (m_pendingEnabledRequest == EnabledRequest::RequestEnabled);
      if (m_enabled != newEnabledState) {
        m_enabled = newEnabledState;
        setDirty(true);
      }
      m_pendingEnabledRequest = EnabledRequest::NoRequest;
    }
    
    // Resolve blend strength if any component made a request
    if (m_pendingMaxBlendStrength > kEmptyBlendStrengthRequest) {
      if (m_blendStrength != m_pendingMaxBlendStrength) {
        m_blendStrength = m_pendingMaxBlendStrength;
        setBlendStrengthDirty(true);
      }
      m_pendingMaxBlendStrength = kEmptyBlendStrengthRequest;
    }
    
    // Resolve blend threshold if any component made a request
    if (m_pendingMinBlendThreshold < kEmptyBlendThresholdRequest) {
      if (m_blendThreshold != m_pendingMinBlendThreshold) {
        m_blendThreshold = m_pendingMinBlendThreshold;
        setDirty(true);
      }
      m_pendingMinBlendThreshold = kEmptyBlendThresholdRequest;
    }
  }

}

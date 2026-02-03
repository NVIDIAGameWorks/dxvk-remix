/*
* Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
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
  void fillHashVector(const std::vector<std::string>& rawInput, std::vector<XXH64_hash_t>& hashVectorOutput) {
    for (auto&& hashStr : rawInput) {
      const XXH64_hash_t h = std::stoull(hashStr, nullptr, 16);
      hashVectorOutput.emplace_back(h);
    }
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
      HashSetLayer hashSet;
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
      case OptionType::HashSet:      new (&storage.hashSet) HashSetLayer(); break;
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
      case OptionType::HashSet:      storage.hashSet.~HashSetLayer(); break;
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
      value.hashSet = new HashSetLayer();
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

  void releaseGenericValue(GenericValue& value, OptionType type) {
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
  }

  bool RtxOptionImpl::s_isInitialized = false;

  bool RtxOptionImpl::registerOption(XXH64_hash_t hash, RtxOptionImpl* option) {
    auto& globalOptions = getGlobalOptionMap();
    if (globalOptions.find(hash) != globalOptions.end()) {
      return false;  // Option with this hash already exists
    }
    globalOptions[hash] = option;
    return true;
  }

  RtxOptionImpl::~RtxOptionImpl() {
    m_onChangeCallback = nullptr;

    // PrioritizedValue destructor handles cleanup of m_optionLayerValueQueue entries

    releaseGenericValue(m_resolvedValue, m_type);
  }
  
  const GenericValue* RtxOptionImpl::getGenericValue(const RtxOptionLayer* layer) const {
    if (!layer) {
      return nullptr;
    }
    
    auto it = m_optionLayerValueQueue.find(layer->getLayerKey());
    if (it != m_optionLayerValueQueue.end()) {
      return &it->second.value;
    }
    
    return nullptr;
  }

  GenericValue* RtxOptionImpl::getOrCreateGenericValue(const RtxOptionLayer* layer) {
    if (!layer) {
      Logger::warn("[RTX Option]: getOrCreateGenericValue called with null layer.");
      return nullptr;
    }

    RtxOptionLayerKey layerKey = layer->getLayerKey();
    auto it = m_optionLayerValueQueue.find(layerKey);
    if (it != m_optionLayerValueQueue.end()) {
      // Mark that this layer has values (in case it wasn't set before)
      layer->setHasValues(true);
      return &it->second.value;
    }
    
    // Create a new entry for this layer
    GenericValue newValue = createGenericValue(m_type);
    PrioritizedValue prioritizedValue(newValue, m_type, layer->getBlendStrength(), layer->getBlendStrengthThreshold());
    auto [insertIt, inserted] = m_optionLayerValueQueue.emplace(layerKey, std::move(prioritizedValue));
    
    if (!inserted) {
      // PrioritizedValue RAII handles cleanup when the temporary is destroyed on failed insert
      Logger::warn("[RTX Option]: Failed to insert layer value for option: " + std::string(m_name));
      return nullptr;
    }
    
    // Mark that this layer now has values
    layer->setHasValues(true);
    
    return &insertIt->second.value;
  }

  const char* RtxOptionImpl::getTypeString() const {
    switch (m_type) {
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

  std::string RtxOptionImpl::genericValueToString(const GenericValue& value) const {
    switch (m_type) {
    case OptionType::Bool: return Config::generateOptionString(value.b);
    case OptionType::Int: return Config::generateOptionString(value.i);
    case OptionType::Float: return Config::generateOptionString(value.f);
    case OptionType::HashSet: return value.hashSet->toString();
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

  std::string RtxOptionImpl::getResolvedValueAsString() const {
    return genericValueToString(m_resolvedValue);
  }

  void RtxOptionImpl::invokeOnChangeCallback(DxvkDevice* device) const {
    if (m_onChangeCallback) {
      m_onChangeCallback(device);
    }
  }

  void RtxOptionImpl::markDirty() {
    RtxOptionManager::getDirtyOptionMap()[m_hash] = this;
  }

  bool RtxOptionImpl::isDirty() const {
    auto& dirtyOptions = RtxOptionManager::getDirtyOptionMap();
    return dirtyOptions.find(m_hash) != dirtyOptions.end();
  }

  bool RtxOptionImpl::clampValue(GenericValue& value) {
    switch (m_type) {
    case OptionType::Int: {
      int oldVal = value.i;
      if (minValue) { value.i = std::max(value.i, minValue->i); }
      if (maxValue) { value.i = std::min(value.i, maxValue->i); }
      return value.i != oldVal;
    }
    case OptionType::Float: {
      float oldVal = value.f;
      if (minValue) { value.f = std::max(value.f, minValue->f); }
      if (maxValue) { value.f = std::min(value.f, maxValue->f); }
      return value.f != oldVal;
    }
    case OptionType::Vector2: {
      Vector2 oldVal = *value.v2;
      if (minValue) { *value.v2 = max(*value.v2, *minValue->v2); }
      if (maxValue) { *value.v2 = min(*value.v2, *maxValue->v2); }
      return *value.v2 != oldVal;
    }
    case OptionType::Vector3: {
      Vector3 oldVal = *value.v3;
      if (minValue) { *value.v3 = max(*value.v3, *minValue->v3); }
      if (maxValue) { *value.v3 = min(*value.v3, *maxValue->v3); }
      return *value.v3 != oldVal;
    }
    case OptionType::Vector4: {
      Vector4 oldVal = *value.v4;
      if (minValue) { *value.v4 = max(*value.v4, *minValue->v4); }
      if (maxValue) { *value.v4 = min(*value.v4, *maxValue->v4); }
      return *value.v4 != oldVal;
    }
    case OptionType::Vector2i: {
      Vector2i oldVal = *value.v2i;
      if (minValue) { *value.v2i = max(*value.v2i, *minValue->v2i); }
      if (maxValue) { *value.v2i = min(*value.v2i, *maxValue->v2i); }
      return *value.v2i != oldVal;
    }
    default:
      return false;
    }
  }


  void RtxOptionImpl::readValue(const Config& options, const std::string& fullName, GenericValue& value) {
    switch (m_type) {
    case OptionType::Bool:
      value.b = options.getOption<bool>(fullName.c_str(), value.b);
      break;
    case OptionType::Int:
      value.i = options.getOption<int>(fullName.c_str(), value.i);
      break;
    case OptionType::Float:
      value.f = options.getOption<float>(fullName.c_str(), value.f);
      break;
    case OptionType::HashSet:
      value.hashSet->parseFromStrings(options.getOption<std::vector<std::string>>(fullName.c_str()));
      break;
    case OptionType::HashVector:
      fillHashVector(options.getOption<std::vector<std::string>>(fullName.c_str()), *value.hashVector);
      break;
    case OptionType::VirtualKeys:
      *value.virtualKeys = options.getOption<VirtualKeys>(fullName.c_str(), *value.virtualKeys);
      break;
    case OptionType::Vector2:
      *value.v2 = options.getOption<Vector2>(fullName.c_str(), *value.v2);
      break;
    case OptionType::Vector3:
      *value.v3 = options.getOption<Vector3>(fullName.c_str(), *value.v3);
      break;
    case OptionType::Vector2i:
      *value.v2i = options.getOption<Vector2i>(fullName.c_str(), *value.v2i);
      break;
    case OptionType::String:
      *value.string = options.getOption<std::string>(fullName.c_str(), *value.string);
      break;
    case OptionType::Vector4:
      *value.v4 = options.getOption<Vector4>(fullName.c_str(), *value.v4);
      break;
    default:
      break;
    }
  }

  void RtxOptionImpl::readOption(const Config& options, const RtxOptionLayer* layer) {
    if (!layer) {
      return;
    }
    
    const std::string fullName = getFullName();
    
    // Check if the option exists in config
    if (!options.findOption(fullName.c_str())) {
      return;
    }
    
    // Read into the specified layer (create entry since we know option exists)
    GenericValue* layerValue = getOrCreateGenericValue(layer);
    if (layerValue) {
      readValue(options, fullName, *layerValue);
    }
    
    markDirty();
  }

  bool RtxOptionImpl::loadFromEnvironmentVariable(const RtxOptionLayer* envLayer, std::string* outValue) {
    // Skip options without environment variable names
    if (m_environment == nullptr || strlen(m_environment) == 0) {
      return false;
    }
    
    // Check if the environment variable is set
    const std::string envVarValue = env::getEnvVar(m_environment);
    if (envVarValue.empty()) {
      return false;
    }
    
    // Return the value if requested
    if (outValue) {
      *outValue = envVarValue;
    }
    
    // Get or create a value in the environment layer
    GenericValue* layerValue = getOrCreateGenericValue(envLayer);
    if (!layerValue) {
      Logger::warn(str::format("[RTX Option]: Failed to create environment layer value for: ", getFullName()));
      return false;
    }
    
    // Parse the environment variable value based on the option type
    switch (m_type) {
    case OptionType::Bool:
      Config::parseOptionValue(envVarValue, layerValue->b);
      break;
    case OptionType::Int:
      Config::parseOptionValue(envVarValue, layerValue->i);
      break;
    case OptionType::Float:
      Config::parseOptionValue(envVarValue, layerValue->f);
      break;
    case OptionType::Vector2:
      Config::parseOptionValue(envVarValue, *layerValue->v2);
      break;
    case OptionType::Vector3:
      Config::parseOptionValue(envVarValue, *layerValue->v3);
      break;
    case OptionType::Vector2i:
      Config::parseOptionValue(envVarValue, *layerValue->v2i);
      break;
    case OptionType::Vector4:
      Config::parseOptionValue(envVarValue, *layerValue->v4);
      break;
    case OptionType::String:
      Config::parseOptionValue(envVarValue, *layerValue->string);
      break;
    case OptionType::VirtualKeys:
      Config::parseOptionValue(envVarValue, *layerValue->virtualKeys);
      break;
    case OptionType::HashSet: {
      std::vector<std::string> hashStrings;
      Config::parseOptionValue(envVarValue, hashStrings);
      layerValue->hashSet->parseFromStrings(hashStrings);
      break;
    }
    case OptionType::HashVector: {
      std::vector<std::string> hashStrings;
      Config::parseOptionValue(envVarValue, hashStrings);
      fillHashVector(hashStrings, *layerValue->hashVector);
      break;
    }
    default:
      Logger::warn(str::format("[RTX Option]: Unsupported type for environment variable: ", getFullName()));
      return false;
    }
    
    markDirty();
    return true;
  }

  void RtxOptionImpl::writeOption(Config& options, const RtxOptionLayer* layer, bool changedOptionOnly) {
    if (m_flags & (uint32_t)RtxOptionFlags::NoSave) {
      return;
    }
    if (!layer) {
      return;
    }
    const GenericValue* value = getGenericValue(layer);
    if (!value) {
      return;
    }

    if (changedOptionOnly && isLayerValueRedundant(layer)) {
      return;
    }
    
    std::string fullName = getFullName();

    switch (m_type) {
    case OptionType::Bool:
      options.setOption(fullName.c_str(), value->b);
      break;
    case OptionType::Int:
      options.setOption(fullName.c_str(), value->i);
      break;
    case OptionType::Float:
      options.setOption(fullName.c_str(), value->f);
      break;
    case OptionType::HashSet:
      options.setOption(fullName.c_str(), value->hashSet->toString());
      break;
    case OptionType::HashVector:
      options.setOption(fullName.c_str(), hashVectorToString(*value->hashVector));
      break;
    case OptionType::VirtualKeys:
      options.setOption(fullName.c_str(), buildKeyBindDescriptorString(*value->virtualKeys));
      break;
    case OptionType::Vector2:
      options.setOption(fullName.c_str(), *value->v2);
      break;
    case OptionType::Vector3:
      options.setOption(fullName.c_str(), *value->v3);
      break;
    case OptionType::Vector2i:
      options.setOption(fullName.c_str(), *value->v2i);
      break;
    case OptionType::String:
      options.setOption(fullName.c_str(), *value->string);
      break;
    case OptionType::Vector4:
      options.setOption(fullName.c_str(), *value->v4);
      break;
    default:
      break;
    }
  }

  void RtxOptionImpl::insertOptionLayerValue(const GenericValue& value, const RtxOptionLayer* layer) {
    if (layer == nullptr) {
      Logger::warn("[RTX Option]: Cannot insert layer value with null layer pointer.");
      return;
    }
    
    RtxOptionLayerKey key = layer->getLayerKey();
    
    // Check if this exact layer already exists
    auto existingIt = m_optionLayerValueQueue.find(key);
    if (existingIt != m_optionLayerValueQueue.end()) {
      // Update existing value and blend settings
      copyValue(value, existingIt->second.value);
      existingIt->second.blendStrength = layer->getBlendStrength();
      existingIt->second.blendThreshold = layer->getBlendStrengthThreshold();
      layer->setHasValues(true);
      return;
    }

    // Create new value and copy from source
    GenericValue optionLayerValue = createGenericValue(m_type);
    copyValue(value, optionLayerValue);

    PrioritizedValue newValue(optionLayerValue, m_type, layer->getBlendStrength(), layer->getBlendStrengthThreshold());
    auto [it, inserted] = m_optionLayerValueQueue.emplace(key, std::move(newValue));
    if (!inserted) {
      Logger::warn("[RTX Option]: Duplicate layer " + layer->getLayerKey().toString() + " ignored (only first kept).");
    } else {
      layer->setHasValues(true);
    }
  }

  void RtxOptionImpl::readOptionLayer(const RtxOptionLayer& optionLayer) {
    GenericValueWrapper value(m_type);
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
    
    auto it = m_optionLayerValueQueue.find(layer->getLayerKey());
    if (it != m_optionLayerValueQueue.end()) {
      // When removing a layer, dirty current option
      markDirty();
      m_optionLayerValueQueue.erase(it);
    }
  }

  void RtxOptionImpl::forEachLayerValue(std::function<bool(const RtxOptionLayer*, const GenericValue&)> callback,
                                         std::optional<XXH64_hash_t> hash,
                                         bool includeInactiveLayers) const {
    std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
    
    const bool isFloatType = (m_type == OptionType::Float || m_type == OptionType::Vector2 ||
                              m_type == OptionType::Vector3 || m_type == OptionType::Vector4);
    
    for (const auto& [layerKey, prioritizedValue] : m_optionLayerValueQueue) {
      // Skip inactive layers unless explicitly requested
      // Use per-option captured blend values for filtering (not layer's current values)
      const bool isActive = isFloatType || (prioritizedValue.blendStrength >= prioritizedValue.blendThreshold);
      if (!isActive && !includeInactiveLayers) {
        continue;
      }

      // For hash sets, if a hash is specified, only include layers that have an opinion on that hash.
      if (m_type == OptionType::HashSet && hash.has_value()) {
        const HashSetLayer* hashSet = prioritizedValue.value.hashSet;
        if (!hashSet || hashSet->empty() || !hashSet->hasPositive(*hash) && !hashSet->hasNegative(*hash)) {
          continue;
        }
      }
      
      // Look up the layer by its key
      RtxOptionLayer* layer = RtxOptionManager::getLayer(layerKey);
      if (layer && !callback(layer, prioritizedValue.value)) {
        break;
      }
    }
  }

  const RtxOptionLayer* RtxOptionImpl::getBlockingLayer(const RtxOptionLayer* targetLayer,
                                                         std::optional<XXH64_hash_t> hash) const {
    const RtxOptionLayer* blocking = nullptr;
    const RtxOptionLayer* target = getTargetLayer(targetLayer);
    const RtxOptionLayerKey targetKey = target ? target->getLayerKey() : kRtxOptionLayerDefaultKey;
    
    forEachLayerValue([&blocking, &targetKey](const RtxOptionLayer* layer, const GenericValue&) {
      if (layer->getLayerKey() < targetKey) {
        blocking = layer;
        return false; // Stop at first match
      }
      return true; // Continue - this layer isn't stronger
    }, hash);
    
    return blocking;
  }

  void RtxOptionImpl::clearFromStrongerLayers(const RtxOptionLayer* targetLayer,
                                               std::optional<XXH64_hash_t> hash) {
    const RtxOptionLayer* target = getTargetLayer(targetLayer);
    const RtxOptionLayerKey targetKey = target ? target->getLayerKey() : kRtxOptionLayerDefaultKey;
    
    bool anyModified = false;
    
    // Iterate through layers stronger than target (keys < targetKey)
    // Map is sorted by RtxOptionLayerKey, strongest (lowest key) first
    auto it = m_optionLayerValueQueue.begin();
    while (it != m_optionLayerValueQueue.end() && it->first < targetKey) {
      RtxOptionLayer* layer = RtxOptionManager::getLayer(it->first);
      
      bool modifiedThisLayer = false;
      bool shouldErase = false;
      
      if (m_type == OptionType::HashSet && hash.has_value()) {
        // For hash sets with specific hash, clear just that hash
        if (it->second.value.hashSet) {
          // Only clear if this layer has an opinion on this hash
          if (it->second.value.hashSet->hasPositive(*hash) || it->second.value.hashSet->hasNegative(*hash)) {
            it->second.value.hashSet->clear(*hash);
            modifiedThisLayer = true;
            if (it->second.value.hashSet->empty()) {
              shouldErase = true;
            }
          }
        }
      } else {
        shouldErase = true;
        modifiedThisLayer = true;
      }
      
      if (modifiedThisLayer) {
        anyModified = true;
        if (layer) {
          layer->onLayerValueChanged();
        }
      }
      
      if (shouldErase) {
        it = m_optionLayerValueQueue.erase(it);
      } else {
        ++it;
      }
    }
    
    if (anyModified) {
      markDirty();
    }
  }

  void RtxOptionImpl::moveLayerValue(const RtxOptionLayer* sourceLayer, const RtxOptionLayer* destLayer) {
    if (!sourceLayer || !destLayer) {
      return;
    }
    if (destLayer->getLayerKey() == kRtxOptionLayerDefaultKey) {
      return; // Can't modify default layer
    }

    // Get source layer value
    auto sourceIt = m_optionLayerValueQueue.find(sourceLayer->getLayerKey());
    if (sourceIt == m_optionLayerValueQueue.end()) {
      return;
    }

    // Get or create dest layer value
    GenericValue* destValue = getOrCreateGenericValue(destLayer);
    if (!destValue) {
      return;
    }

    // Move based on type
    switch (m_type) {
    case OptionType::HashSet: {
      HashSetLayer* sourceHashSet = sourceIt->second.value.hashSet;
      if (!sourceHashSet || sourceHashSet->empty() || !destValue->hashSet) {
        break;
      }
      // Union merge for hash sets
      destValue->hashSet->mergeFrom(*sourceHashSet);
      sourceHashSet->clearAll();
      break;
    }
    default:
      // For all other types, overwrite the dest value
      copyValue(sourceIt->second.value, *destValue);
      break;
    }

    // Notify dest layer that a value was added
    destLayer->onLayerValueChanged();
    
    // Remove source layer value and notify it changed
    disableLayerValue(sourceLayer);
    sourceLayer->onLayerValueChanged();
    
    markDirty();
  }

  bool RtxOptionImpl::migrateValuesTo(RtxOptionImpl* destOption) {
    if (!destOption) {
      return false;
    }

    // Verify types match
    if (m_type != destOption->m_type) {
      assert(false && "Cannot migrate option: type mismatch");
      return false;
    }

    Logger::info(str::format("[Migration] Migrating from ", getFullName(), " to ", destOption->getFullName()));

    bool anyMigrated = false;

    // Collect keys to remove after iteration (can't modify map while iterating)
    std::vector<RtxOptionLayerKey> keysToRemove;

    // Migrate data from each layer
    for (auto& [layerKey, sourcePrioritizedValue] : m_optionLayerValueQueue) {
      // Skip the default layer - we keep it
      if (layerKey == kRtxOptionLayerDefaultKey) {
        continue;
      }

      // Find or create the destination layer entry
      auto destIt = destOption->m_optionLayerValueQueue.find(layerKey);
      GenericValue* destValue = nullptr;

      if (destIt != destOption->m_optionLayerValueQueue.end()) {
        destValue = &destIt->second.value;
      } else {
        // Create a new entry using the source's blend values
        GenericValue newValue = createGenericValue(m_type);
        PrioritizedValue prioritizedValue(newValue, m_type, sourcePrioritizedValue.blendStrength, sourcePrioritizedValue.blendThreshold);
        auto [insertIt, inserted] = destOption->m_optionLayerValueQueue.emplace(layerKey, std::move(prioritizedValue));
        if (inserted) {
          destValue = &insertIt->second.value;
        }
      }

      if (!destValue) {
        continue;
      }

      switch (m_type) {
      case OptionType::HashSet: {
        HashSetLayer* sourceHashSet = sourcePrioritizedValue.value.hashSet;
        if (!sourceHashSet || sourceHashSet->empty() || !destValue->hashSet) {
          break;
        }

        // Union merge for hash sets
        destValue->hashSet->mergeFrom(*sourceHashSet);

        anyMigrated = true;
        break;
      }
      default: {
        // For non-hashset types, only migrate if destination didn't already have data in this layer
        if (destIt == destOption->m_optionLayerValueQueue.end()) {
          destOption->copyValue(sourcePrioritizedValue.value, *destValue);
          anyMigrated = true;
        }
        break;
      }
      }

      // Mark this layer for removal from source
      keysToRemove.push_back(layerKey);
    }

    // Remove all migrated layers from source, leaving only the default
    // PrioritizedValue RAII handles cleanup when erase destroys the element
    for (const auto& key : keysToRemove) {
      m_optionLayerValueQueue.erase(key);
    }

    if (anyMigrated) {
      destOption->markDirty();
      markDirty();
    }

    return anyMigrated;
  }

  void RtxOptionImpl::updateLayerBlendStrength(const RtxOptionLayer& optionLayer) {
    // Find the option layer value by exact layer match - update blend strength for ALL options
    // in this layer, including runtime values set via setDeferred (not just config-loaded options)
    auto optionLayerIter = m_optionLayerValueQueue.find(optionLayer.getLayerKey());
    if (optionLayerIter != m_optionLayerValueQueue.end()) {
      const float newStrength = optionLayer.getBlendStrength();
      const float newThreshold = optionLayer.getBlendStrengthThreshold();
      
      // Only mark dirty if blend settings actually changed
      if (optionLayerIter->second.blendStrength != newStrength ||
          optionLayerIter->second.blendThreshold != newThreshold) {
        optionLayerIter->second.blendStrength = newStrength;
        optionLayerIter->second.blendThreshold = newThreshold;
        markDirty();
      }
    }
  }

  const RtxOptionLayer* RtxOptionImpl::getTargetLayer(const RtxOptionLayer* explicitLayer) const {
    // NoSave options should always go to the Derived layer, never to saved config layers
    // This overrides even explicit layer specifications
    if ((m_flags & RtxOptionFlags::NoSave) != 0) {
      return RtxOptionLayer::getDerivedLayer();
    }
    
    // When an explicit layer is passed, use it (for migration testing and direct layer control)
    if (explicitLayer) {
      return explicitLayer;
    }
    
    const RtxOptionEditTarget editTarget = RtxOptionLayerTarget::getEditTarget();
    const bool hasUserSettingsFlag = (m_flags & RtxOptionFlags::UserSetting) != 0;
    
    // User-driven changes (UI edit target)
    if (editTarget == RtxOptionEditTarget::User) {
      // Options with UserSettings flag go to User Settings layer
      if (hasUserSettingsFlag) {
        return RtxOptionLayer::getUserLayer();
      }
      // Options without UserSettings flag go to Remix Config layer
      return RtxOptionLayer::getRtxConfLayer();
    }
    
    // Code-driven changes (Derived edit target)
    if (editTarget == RtxOptionEditTarget::Derived) {
      // Options with UserSettings flag go to Quality layer (or User Settings if Graphics Preset is Custom)
      if (hasUserSettingsFlag) {
        // Special case: When Graphics Preset is Custom, redirect UserSettings options to User Settings layer
        // Use getValueNoLock() to avoid mutex deadlock (we're already inside a locked section)
        const GraphicsPreset currentPreset = RtxOptions::graphicsPresetObject().getValueNoLock();
        if (currentPreset == GraphicsPreset::Custom) {
          return RtxOptionLayer::getUserLayer();
        }
        return RtxOptionLayer::getQualityLayer();
      }
      // Options without UserSettings flag go to Derived layer
      return RtxOptionLayer::getDerivedLayer();
    }
    
    // Shouldn't reach here - all edit targets should be handled above
    assert(false && "Unknown edit target");
    return RtxOptionLayer::getDerivedLayer();
  }

  bool RtxOptionImpl::isDefault() const {
    const GenericValue* defaultValue = getGenericValue(RtxOptionLayer::getDefaultLayer());
    return defaultValue && isEqual(m_resolvedValue, *defaultValue);
  }

  bool RtxOptionImpl::isEqual(const GenericValue& aValue, const GenericValue& bValue) const {
    switch (m_type) {
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

  bool RtxOptionImpl::hasValueInLayer(const RtxOptionLayer* layer, std::optional<XXH64_hash_t> hash) const {
    if (!layer) {
      return false;
    }
    auto it = m_optionLayerValueQueue.find(layer->getLayerKey());
    if (it == m_optionLayerValueQueue.end()) {
      return false;
    }
    // For hash set options, check for specific hash or non-empty set
    if (m_type == OptionType::HashSet) {
      const HashSetLayer* hashSet = it->second.value.hashSet;
      if (!hashSet) {
        return false;
      }
      if (hash.has_value()) {
        // Check if this specific hash is present (positive or negative)
        return hashSet->hasPositive(*hash) || hashSet->hasNegative(*hash);
      }
      return !hashSet->empty();
    }
    return true;
  }

  bool RtxOptionImpl::isLayerValueRedundant(const RtxOptionLayer* layer) const {
    if (!layer) {
      return true;
    }

    // Get the layer's value
    const GenericValue* layerValue = getGenericValue(layer);
    if (!layerValue) {
      return true;
    }

    // Compute what the resolved value would be without this layer (and stronger layers)
    GenericValueWrapper valueWithoutLayer(m_type);
    const_cast<RtxOptionImpl*>(this)->resolveValue(valueWithoutLayer.data, layer);

    // The layer is redundant if its value equals the resolved value without it
    return isEqual(*layerValue, valueWithoutLayer.data);
  }

  void RtxOptionImpl::copyValue(const GenericValue& source, GenericValue& target) {
    switch (m_type) {
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
    switch (m_type) {
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
      // Merge positives and negatives from source to target.
      // Both sets accumulate during resolution, and the final result is computed as positives - negatives.
      target.hashSet->mergeFrom(*source.hashSet);
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

  bool RtxOptionImpl::resolveValue(GenericValue& value, const RtxOptionLayer* excludeLayer) {
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
    GenericValueWrapper optionValue(m_type);
    float throughput = 1.0f;
    bool passedExcludedLayer = (excludeLayer == nullptr);

    // Loop layers from strongest to weakest to lerp the value across layers based on the blend strength of layers
    for (const auto& optionLayer : m_optionLayerValueQueue) {
      // Skip the excluded layer and all stronger layers (earlier in the sorted queue)
      if (!passedExcludedLayer) {
        if (optionLayer.first == excludeLayer->getLayerKey()) {
          // Found the excluded layer - skip it and start including subsequent layers
          passedExcludedLayer = true;
        }
        continue;
      }

      if (m_type == OptionType::Float || m_type == OptionType::Vector2 || m_type == OptionType::Vector3 || m_type == OptionType::Vector4) {
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
        if (m_type != OptionType::HashSet) {
          break;
        }
      }
    }

    // Clamp the resolved value
    clampValue(optionValue.data);

    const bool valueHasChanged = !isEqual(optionValue.data, value);
    if (valueHasChanged) {
      // Copy to m_resolvedValue
      copyValue(optionValue.data, value);
    }
    return valueHasChanged;
  }

}  // namespace dxvk

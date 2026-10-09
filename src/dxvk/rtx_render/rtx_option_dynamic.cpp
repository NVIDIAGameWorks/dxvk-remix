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

#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <mutex>
#include <vector>

#include "rtx_option_dynamic.h"
#include "rtx_option_manager.h"
#include "../util/log/log.h"

namespace dxvk {

  namespace {
    constexpr uint32_t kAllowedDynamicFlags = RtxOptionFlags::UserSetting | RtxOptionFlags::NoSave;

    bool isAsciiSpace(char c) {
      return c == ' ' || c == '\t';
    }

    std::string_view trim(std::string_view text) {
      while (!text.empty() && isAsciiSpace(text.front())) {
        text.remove_prefix(1);
      }
      while (!text.empty() && isAsciiSpace(text.back())) {
        text.remove_suffix(1);
      }
      return text;
    }

    bool startsWith(const std::string& text, const std::string& prefix) {
      return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    bool parseBool(std::string_view text, bool& out) {
      std::string lower(text);
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (lower == "true" || lower == "1") {
        out = true;
        return true;
      }
      if (lower == "false" || lower == "0") {
        out = false;
        return true;
      }
      return false;
    }

    // from_chars rejects a leading '+'; accept one on the mantissa only.
    std::string_view stripPlus(std::string_view text) {
      if (text.size() > 1 && text.front() == '+' && text[1] != '+' && text[1] != '-') {
        text.remove_prefix(1);
      }
      return text;
    }

    bool parseInt(std::string_view text, int32_t& out) {
      text = stripPlus(trim(text));
      if (text.empty()) {
        return false;
      }
      const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
      return ec == std::errc() && end == text.data() + text.size();
    }

    bool parseFloat(std::string_view text, float& out) {
      text = stripPlus(trim(text));
      if (text.empty()) {
        return false;
      }
      // Rejects inf, nan and hex floats, which from_chars would otherwise accept.
      for (const char c : text) {
        if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) {
          return false;
        }
      }
      const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
      return ec == std::errc() && end == text.data() + text.size() && std::isfinite(out);
    }

    template<typename T, typename ParseFn>
    bool parseComponents(std::string_view text, T* out, size_t count, ParseFn parseComponent) {
      size_t index = 0;
      while (true) {
        const size_t comma = text.find(',');
        if (index >= count || !parseComponent(text.substr(0, comma), out[index])) {
          return false;
        }
        ++index;
        if (comma == std::string_view::npos) {
          break;
        }
        text.remove_prefix(comma + 1);
      }
      return index == count;
    }

    bool isValidDynamicString(std::string_view text) {
      if (text.empty() || trim(text).size() != text.size()) {
        return false;
      }
      for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || u < 0x20 || u == 0x7f) {
          return false;
        }
      }
      return true;
    }

    bool isSupportedDynamicType(OptionType type) {
      switch (type) {
      case OptionType::Bool:
      case OptionType::Int:
      case OptionType::Float:
      case OptionType::String:
      case OptionType::Vector2:
      case OptionType::Vector3:
      case OptionType::Vector4:
      case OptionType::Vector2i:
        return true;
      default:
        return false;
      }
    }

    bool parseNamespaceDefault(const DynamicOptionNamespace& ns, GenericValue& value) {
      if (ns.type == OptionType::String && ns.defaultValue.empty()) {
        return true;
      }
      return parseDynamicValue(ns.type, ns.defaultValue, value);
    }

    DynamicOptionValue toDynamicValue(OptionType type, const GenericValue& value) {
      switch (type) {
      case OptionType::Bool: return value.b;
      case OptionType::Int: return static_cast<int32_t>(value.i);
      case OptionType::Float: return value.f;
      case OptionType::String: return *value.string;
      case OptionType::Vector2: return *value.v2;
      case OptionType::Vector3: return *value.v3;
      case OptionType::Vector4: return *value.v4;
      case OptionType::Vector2i: return *value.v2i;
      default:
        assert(false && "Unsupported dynamic option type");
        return false;
      }
    }

    struct NamespaceRegistry {
      std::mutex mutex;
      std::vector<DynamicOptionNamespace> namespaces;
    };

    NamespaceRegistry& getNamespaceRegistry() {
      static NamespaceRegistry registry;
      return registry;
    }

    std::optional<DynamicOptionNamespace> findOwningNamespace(const std::string& key) {
      NamespaceRegistry& registry = getNamespaceRegistry();
      std::lock_guard<std::mutex> lock(registry.mutex);
      for (const DynamicOptionNamespace& ns : registry.namespaces) {
        if (startsWith(key, ns.prefix)) {
          return ns;
        }
      }
      return std::nullopt;
    }

    bool hasNamespaces() {
      NamespaceRegistry& registry = getNamespaceRegistry();
      std::lock_guard<std::mutex> lock(registry.mutex);
      return !registry.namespaces.empty();
    }

    // Owns the layer name; RtxOptionLayerKey::name points into the layer, which may be released before the drain.
    struct OwnedLayerKey {
      uint32_t priority;
      std::string name;
      uint64_t generation;
    };

    struct DynamicValueRequest {
      std::string key;
      std::string value;
      std::optional<OwnedLayerKey> layer;  // nullopt: the user layer, resolved when the request is applied
      bool clear = false;
    };

    OwnedLayerKey makeOwnedLayerKey(const RtxOptionLayer& layer) {
      const RtxOptionLayerKey& key = layer.getLayerKey();
      return { key.priority, std::string(key.name), layer.getGeneration() };
    }

    struct DynamicValueRequestQueue {
      std::mutex mutex;
      std::vector<DynamicValueRequest> requests;
    };

    DynamicValueRequestQueue& getDynamicValueRequestQueue() {
      static DynamicValueRequestQueue queue;
      return queue;
    }

    void pushDynamicValueRequest(DynamicValueRequest request) {
      DynamicValueRequestQueue& queue = getDynamicValueRequestQueue();
      std::lock_guard<std::mutex> lock(queue.mutex);
      queue.requests.push_back(std::move(request));
    }

    DynamicOptionResult validateDynamicKey(const std::string& key, std::optional<DynamicOptionNamespace>& ns) {
      ns = findOwningNamespace(key);
      if (!ns) {
        Logger::warn(str::format("[RTX Option]: '", key, "' is not an RtxOption or a key in a registered dynamic namespace."));
        return DynamicOptionResult::NotOwned;
      }
      if (!isValidDynamicSuffix(std::string_view(key).substr(ns->prefix.size()))) {
        Logger::warn(str::format("[RTX Option]: Invalid key '", key, "' in dynamic namespace '", ns->prefix, "'."));
        return DynamicOptionResult::InvalidArgument;
      }
      const auto globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
      auto it = globalRtxOptions->find(StringToXXH64(key, 0));
      if (it != globalRtxOptions->end() && (!it->second->isDynamic() || it->second->getFullName() != key)) {
        Logger::warn(str::format("[RTX Option]: Dynamic option '", key, "' collides with existing option '", it->second->getFullName(), "'."));
        return DynamicOptionResult::Conflict;
      }
      return DynamicOptionResult::Success;
    }

    bool validateDynamicValueText(const DynamicOptionNamespace& ns, const std::string& key, const std::string& value) {
      GenericValue parsedValue = createGenericValue(ns.type);
      const bool parsed = parseDynamicValue(ns.type, value, parsedValue);
      releaseGenericValue(parsedValue, ns.type);
      if (!parsed) {
        Logger::warn(str::format("[RTX Option]: Invalid value '", value, "' for dynamic option '", key, "'."));
      }
      return parsed;
    }

    // Never deleted; the registry holds raw pointers for the process lifetime.
    class DynamicRtxOption final : public RtxOptionImpl {
    public:
      DynamicRtxOption(const DynamicOptionNamespace& ns, const std::string& fullName, const GenericValue& defaultValue)
        : RtxOptionImpl(StringToXXH64(fullName, 0), "", "", ns.type, "")
        , m_ownedCategory(ns.prefix.substr(0, ns.prefix.size() - 1))
        , m_ownedName(fullName.substr(ns.prefix.size()))
        , m_ownedDescription(ns.description) {
        m_category = m_ownedCategory.c_str();
        m_name = m_ownedName.c_str();
        m_description = m_ownedDescription.c_str();
        m_isDynamic = true;
        m_flags.store(ns.flags, std::memory_order_relaxed);
        m_resolvedValue = createGenericValue(m_type);
        copyValue(defaultValue, m_resolvedValue);
        insertOptionLayerValue(m_resolvedValue, RtxOptionLayer::getDefaultLayer());
      }

    private:
      std::string m_ownedCategory;
      std::string m_ownedName;
      std::string m_ownedDescription;
    };
  }

  bool isValidDynamicSuffix(std::string_view suffix) {
    if (suffix.empty() || suffix.front() == '.' || suffix.back() == '.') {
      return false;
    }
    char previous = '\0';
    for (const char c : suffix) {
      const bool isWordChar = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
      if (!isWordChar && !(c == '.' && previous != '.')) {
        return false;
      }
      previous = c;
    }
    return true;
  }

  bool parseDynamicValue(OptionType type, std::string_view text, GenericValue& value) {
    switch (type) {
    case OptionType::Bool:
      return parseBool(trim(text), value.b);
    case OptionType::Int:
      return parseInt(text, value.i);
    case OptionType::Float:
      return parseFloat(text, value.f);
    case OptionType::String:
      text = trim(text);
      if (!isValidDynamicString(text)) {
        return false;
      }
      *value.string = std::string(text);
      return true;
    case OptionType::Vector2:
      return parseComponents(trim(text), value.v2->data, 2, parseFloat);
    case OptionType::Vector3:
      return parseComponents(trim(text), value.v3->data, 3, parseFloat);
    case OptionType::Vector4:
      return parseComponents(trim(text), value.v4->data, 4, parseFloat);
    case OptionType::Vector2i:
      return parseComponents(trim(text), value.v2i->data, 2, parseInt);
    default:
      return false;
    }
  }

  RtxOptionImpl* RtxOptionManager::findOrCreateDynamicOptionLocked(const DynamicOptionNamespace& ns, const std::string& key,
                                                                   std::vector<RtxOptionImpl*>& created) {
    const XXH64_hash_t hash = StringToXXH64(key, 0);
    for (RtxOptionImpl* option : created) {
      if (option->m_hash == hash) {
        return option->getFullName() == key ? option : nullptr;
      }
    }

    const auto globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
    auto it = globalRtxOptions->find(hash);
    if (it != globalRtxOptions->end()) {
      if (it->second->isDynamic() && it->second->getFullName() == key) {
        return it->second;
      }
      Logger::warn(str::format("[RTX Option]: Dynamic option '", key, "' collides with existing option '", it->second->getFullName(), "'."));
      return nullptr;
    }

    if (!isValidDynamicSuffix(std::string_view(key).substr(ns.prefix.size()))) {
      Logger::warn(str::format("[RTX Option]: Ignoring invalid key '", key, "' in dynamic namespace '", ns.prefix, "'."));
      return nullptr;
    }

    GenericValue defaultValue = createGenericValue(ns.type);
    [[maybe_unused]] const bool parsed = parseNamespaceDefault(ns, defaultValue);
    assert(parsed && "Namespace defaults are validated at registration");
    RtxOptionImpl* option = new DynamicRtxOption(ns, key, defaultValue);
    releaseGenericValue(defaultValue, ns.type);
    created.push_back(option);
    return option;
  }

  bool RtxOptionManager::registerDynamicNamespace(const DynamicOptionNamespace& ns) {
    const std::string& prefix = ns.prefix;
    if (prefix.size() < 6 || !startsWith(prefix, "rtx.") || prefix.back() != '.' ||
        !isValidDynamicSuffix(std::string_view(prefix).substr(0, prefix.size() - 1))) {
      Logger::err(str::format("[RTX Option]: Invalid dynamic namespace prefix '", prefix, "'."));
      return false;
    }
    if (!isSupportedDynamicType(ns.type)) {
      Logger::err(str::format("[RTX Option]: Unsupported type for dynamic namespace '", prefix, "'."));
      return false;
    }
    if ((ns.flags & ~kAllowedDynamicFlags) != 0) {
      Logger::err(str::format("[RTX Option]: Unsupported flags for dynamic namespace '", prefix, "'."));
      return false;
    }
    {
      GenericValue defaultValue = createGenericValue(ns.type);
      const bool parsed = parseNamespaceDefault(ns, defaultValue);
      releaseGenericValue(defaultValue, ns.type);
      if (!parsed) {
        Logger::err(str::format("[RTX Option]: Invalid default '", ns.defaultValue, "' for dynamic namespace '", prefix, "'."));
        return false;
      }
    }

    // Registration and replay share the write lock with discovery, so no option in this namespace
    // can be created before its layers are replayed.
    std::lock_guard<std::mutex> layerLock(s_layerMutex);
    std::lock_guard<std::mutex> updateLock(RtxOptionImpl::getUpdateMutex());
    std::lock_guard<std::mutex> writeLock(RtxOptionImpl::getRegistryWriteMutex());

    {
      const auto globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
      for (const auto& [hash, optionPtr] : *globalRtxOptions) {
        if (!optionPtr->isDynamic() && startsWith(optionPtr->getFullName(), prefix)) {
          Logger::err(str::format("[RTX Option]: Dynamic namespace '", prefix, "' contains existing option '", optionPtr->getFullName(), "'."));
          return false;
        }
      }
    }

    {
      NamespaceRegistry& registry = getNamespaceRegistry();
      std::lock_guard<std::mutex> lock(registry.mutex);
      for (const DynamicOptionNamespace& existing : registry.namespaces) {
        if (existing.prefix == prefix) {
          if (existing == ns) {
            return true;
          }
          Logger::err(str::format("[RTX Option]: Dynamic namespace '", prefix, "' is already registered with a different definition."));
          return false;
        }
        if (startsWith(existing.prefix, prefix) || startsWith(prefix, existing.prefix)) {
          Logger::err(str::format("[RTX Option]: Dynamic namespace '", prefix, "' overlaps '", existing.prefix, "'."));
          return false;
        }
      }
      registry.namespaces.push_back(ns);
    }

    std::vector<const RtxOptionLayer*> enabledLayers;
    for (const auto& [layerKey, layer] : getLayerRegistry()) {
      if (layer->isEnabled() && layer->isValid()) {
        enabledLayers.push_back(layer.get());
      }
    }

    std::vector<RtxOptionImpl*> created;
    for (const RtxOptionLayer* layer : enabledLayers) {
      for (const auto& [key, value] : layer->getConfig().getOptions()) {
        if (startsWith(key, prefix)) {
          findOrCreateDynamicOptionLocked(ns, key, created);
        }
      }
    }

    for (RtxOptionImpl* option : created) {
      for (const RtxOptionLayer* layer : enabledLayers) {
        option->readOptionLayer(*layer);
      }
      option->resolveValue(option->m_resolvedValue);
    }
    if (!created.empty()) {
      for (const RtxOptionLayer* layer : enabledLayers) {
        layer->onLayerValueChanged();
      }
    }

    RtxOptionImpl::publishOptionsLocked(created);
    return true;
  }

  DynamicOptionResult RtxOptionManager::queueDynamicValue(const std::string& key, const std::string& value) {
    std::optional<DynamicOptionNamespace> ns;
    const DynamicOptionResult result = validateDynamicKey(key, ns);
    if (result != DynamicOptionResult::Success) {
      return result;
    }
    if (!validateDynamicValueText(*ns, key, value)) {
      return DynamicOptionResult::InvalidArgument;
    }
    std::optional<OwnedLayerKey> layer;
    {
      std::lock_guard<std::mutex> layerLock(s_layerMutex);
      std::lock_guard<std::mutex> updateLock(RtxOptionImpl::getUpdateMutex());
      // Before initialization the user layer is resolved when the request is applied.
      if (const RtxOptionLayer* userLayer = RtxOptionLayer::getUserLayer()) {
        layer = makeOwnedLayerKey(*userLayer);
      }
    }
    pushDynamicValueRequest({ key, value, std::move(layer), false });
    return DynamicOptionResult::Success;
  }

  DynamicOptionResult RtxOptionManager::queueDynamicRequest(const std::string& key, const std::string* value, const RtxOptionLayer* layer) {
    std::optional<DynamicOptionNamespace> ns;
    const DynamicOptionResult result = validateDynamicKey(key, ns);
    if (result != DynamicOptionResult::Success) {
      return result;
    }
    if (value != nullptr && !validateDynamicValueText(*ns, key, *value)) {
      return DynamicOptionResult::InvalidArgument;
    }

    std::optional<OwnedLayerKey> targetKey;
    {
      std::lock_guard<std::mutex> layerLock(s_layerMutex);
      std::lock_guard<std::mutex> updateLock(RtxOptionImpl::getUpdateMutex());
      const RtxOptionLayer* target = RtxOptionImpl::getTargetLayerForFlags(ns->flags, layer);
      // A disabled layer's values would still resolve as active, and it is reloaded from its file when re-enabled.
      if (target != nullptr && target != RtxOptionLayer::getDefaultLayer() && target->isEnabled()) {
        targetKey = makeOwnedLayerKey(*target);
      }
    }
    if (!targetKey) {
      Logger::warn(str::format("[RTX Option]: No editable layer for dynamic option '", key, "'; the target layer is missing or disabled."));
      return DynamicOptionResult::NoTargetLayer;
    }

    pushDynamicValueRequest({ key, value ? *value : std::string(), std::move(targetKey), value == nullptr });
    return DynamicOptionResult::Success;
  }

  DynamicOptionResult RtxOptionManager::setDynamicValue(const std::string& key, const std::string& value, const RtxOptionLayer* layer) {
    return queueDynamicRequest(key, &value, layer);
  }

  DynamicOptionResult RtxOptionManager::clearDynamicValue(const std::string& key, const RtxOptionLayer* layer) {
    return queueDynamicRequest(key, nullptr, layer);
  }

  void RtxOptionManager::applyDynamicValueRequests() {
    const RtxOptionLayer* userLayer = RtxOptionLayer::getUserLayer();
    if (userLayer == nullptr) {
      return;
    }

    std::vector<DynamicValueRequest> requests;
    {
      DynamicValueRequestQueue& queue = getDynamicValueRequestQueue();
      std::lock_guard<std::mutex> lock(queue.mutex);
      requests.swap(queue.requests);
    }
    if (requests.empty()) {
      return;
    }

    std::lock_guard<std::mutex> writeLock(RtxOptionImpl::getRegistryWriteMutex());
    std::vector<RtxOptionImpl*> created;
    for (const DynamicValueRequest& request : requests) {
      const RtxOptionLayer* layer = request.layer
        ? getLayer(RtxOptionLayerKey { request.layer->priority, request.layer->name })
        : userLayer;
      if (layer == nullptr || !layer->isEnabled()) {
        Logger::warn(str::format("[RTX Option]: Dropping change to '", request.key, "'; its layer no longer exists or is disabled."));
        continue;
      }
      if (request.layer && request.layer->generation != layer->getGeneration()) {
        Logger::warn(str::format("[RTX Option]: Dropping change to '", request.key, "'; its layer was reloaded, cleared or replaced."));
        continue;
      }
      const std::optional<DynamicOptionNamespace> ns = findOwningNamespace(request.key);
      if (!ns) {
        continue;
      }

      if (request.clear) {
        // Options created earlier in this batch are not published yet.
        const XXH64_hash_t hash = StringToXXH64(request.key, 0);
        auto createdIt = std::find_if(created.begin(), created.end(), [hash](const RtxOptionImpl* option) {
          return option->m_hash == hash;
        });
        RtxOptionImpl* option = createdIt != created.end() ? *createdIt : RtxOptionImpl::getOptionByFullName(request.key);
        if (option != nullptr && option->isDynamic()) {
          option->disableLayerValue(layer);
        }
        // Clearing also discards a rejected line, so the next save removes it.
        layer->setKeyRejected(request.key, false);
        layer->onLayerValueChanged();
        continue;
      }

      RtxOptionImpl* option = findOrCreateDynamicOptionLocked(*ns, request.key, created);
      if (option == nullptr) {
        continue;
      }
      GenericValue parsedValue = createGenericValue(option->m_type);
      if (parseDynamicValue(option->m_type, request.value, parsedValue)) {
        auto [layerValue, isNew] = option->getOrCreateGenericValue(layer);
        if (layerValue) {
          option->copyValue(parsedValue, *layerValue);
          layer->setKeyRejected(request.key, false);
          layer->onLayerValueChanged();
          option->markDirty();
        }
      }
      releaseGenericValue(parsedValue, option->m_type);
    }
    RtxOptionImpl::publishOptionsLocked(created);
  }

  bool RtxOptionManager::hasOpinion(const RtxOptionImpl& option) {
    const auto& queue = option.m_optionLayerValueQueue;
    return std::any_of(queue.begin(), queue.end(), [](const auto& entry) {
      return !(entry.first == kRtxOptionLayerDefaultKey);
    });
  }

  void RtxOptionManager::discoverDynamicOptions(const RtxOptionLayer& layer) {
    if (!hasNamespaces()) {
      return;
    }
    std::lock_guard<std::mutex> writeLock(RtxOptionImpl::getRegistryWriteMutex());
    std::vector<RtxOptionImpl*> created;
    for (const auto& [key, value] : layer.getConfig().getOptions()) {
      const std::optional<DynamicOptionNamespace> ns = findOwningNamespace(key);
      if (ns) {
        findOrCreateDynamicOptionLocked(*ns, key, created);
      }
    }
    RtxOptionImpl::publishOptionsLocked(created);
  }

  std::optional<DynamicOptionValue> RtxOptionManager::getDynamicValue(const std::string& key, bool withOpinionsOnly) {
    const RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName(key);
    if (option == nullptr || !option->isDynamic()) {
      return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
    if (withOpinionsOnly && !hasOpinion(*option)) {
      return std::nullopt;
    }
    option->tagInvalidationScope();
    return toDynamicValue(option->m_type, option->m_resolvedValue);
  }

  std::vector<DynamicOptionEntry> RtxOptionManager::enumerateDynamicOptions(const std::string& prefix, bool withOpinionsOnly) {
    std::vector<std::pair<std::string, DynamicOptionValue>> matches;
    {
      const auto globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      for (const auto& [hash, option] : *globalRtxOptions) {
        if (!option->isDynamic()) {
          continue;
        }
        std::string fullName = option->getFullName();
        if (!startsWith(fullName, prefix)) {
          continue;
        }
        if (withOpinionsOnly && !hasOpinion(*option)) {
          continue;
        }
        option->tagInvalidationScope();
        matches.emplace_back(std::move(fullName), toDynamicValue(option->m_type, option->m_resolvedValue));
      }
    }

    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<DynamicOptionEntry> entries;
    entries.reserve(matches.size());
    for (auto& [fullName, value] : matches) {
      entries.push_back({ fullName.substr(prefix.size()), std::move(value) });
    }
    return entries;
  }

}  // namespace dxvk

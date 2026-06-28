#ifndef YSC_NATIVE_METADATA_HPP
#define YSC_NATIVE_METADATA_HPP

#include "json/json.h"
#include <binaryninjaapi.h>
#include <cstdint>
#include <string>

uint64_t RotLeft(uint64_t value, uint64_t count);
BinaryNinja::Ref<BinaryNinja::Type> NativeJsonTypeToBN(BinaryNinja::Architecture* arch, const std::string& jsonType);
nlohmann::json::const_iterator FindNativeMetadata(const nlohmann::json& nativeNamespace, uint64_t hash);
std::string NativeSymbolName(const std::string& namespaceName, const std::string& nativeName);

#endif

#include "inc.hpp"
#include "View/NativeMetadata.hpp"
#include <algorithm>
#include <cctype>

uint64_t RotLeft(uint64_t value, uint64_t count)
{
    count &= 63;
    return (value << count) | (value >> (64 - count));
}

static std::string NormalizeNativeType(std::string type)
{
    type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char c) { return std::isspace(c); }), type.end());
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
    return type;
}

BinaryNinja::Ref<BinaryNinja::Type> NativeJsonTypeToBN(BinaryNinja::Architecture* arch, const std::string& jsonType)
{
    using namespace BinaryNinja;
    std::string type = NormalizeNativeType(jsonType);

    if (type.empty() || type == "any" || type == "object" || type == "scrhandle" || type == "hash" || type == "joaat")
        return Type::IntegerType(4, true);
    if (type == "void")
        return Type::VoidType();
    if (type == "bool" || type == "boolean" || type == "BOOL" || type == "BOOL" || type == "BOOL" || type == "BOOL")
        return Type::BoolType();
    if (type == "float")
        return Type::FloatType(4);
    if (type == "double")
        return Type::FloatType(8);
    if (type == "char" || type == "int8_t" || type == "uint8_t")
        return Type::IntegerType(1, type[0] != 'u');
    if (type == "short" || type == "int16_t" || type == "uint16_t")
        return Type::IntegerType(2, type[0] != 'u');
    if (type == "int" || type == "int32_t" || type == "uint" || type == "uint32_t" || type == "dword")
        return Type::IntegerType(4, type[0] != 'u');
    if (type == "long" || type == "int64_t" || type == "uint64_t")
        return Type::IntegerType(8, type[0] != 'u');

    bool isPointer = type.find('*') != std::string::npos || type.ends_with("ptr") || type.ends_with("*");
    if (isPointer || type == "constchar*" || type == "char*" || type == "string")
    {
        bool isString = type.find("char") != std::string::npos || type == "string";
        Ref<Type> pointee = isString ? Type::IntegerType(1, true, "char") : Type::VoidType();
        return Type::PointerType(arch, pointee, isString && type.find("const") != std::string::npos);
    }

    return Type::IntegerType(4, true);
}

nlohmann::json::const_iterator FindNativeMetadata(const nlohmann::json& nativeNamespace, uint64_t hash)
{
    auto find = nativeNamespace.find(fmt::format("0x{:016X}", hash));
    if (find != nativeNamespace.end())
        return find;

    return nativeNamespace.find(fmt::format("0x{:X}", hash));
}

std::string NativeSymbolName(const std::string& namespaceName, const std::string& nativeName)
{
    std::string combined = fmt::format("{}_{}", namespaceName, nativeName);

    return fmt::format("native_{}", combined);
}

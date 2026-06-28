#include "inc.hpp"
#include "Architecture/YSCArchitecture.hpp"

std::string YSCArchitecture::GetRegisterName(uint32_t reg)
{
    if (reg >= Reg_MAX)
        return "UNKREG";
    return std::string(g_RegNames[reg]);
}

BNRegisterInfo YSCArchitecture::GetRegisterInfo(uint32_t reg)
{
    BNRegisterInfo info;
    if (reg < Reg_MAX)
        info.fullWidthRegister = reg;
    info.size = 4;
    info.extend = NoExtend;
    info.offset = 0;
    return info;
}

uint32_t YSCArchitecture::GetStackPointerRegister()
{
    return Reg_SP;
}

std::vector<uint32_t> YSCArchitecture::GetAllRegisters()
{
    std::vector<uint32_t> result;
    for (uint32_t i = 0; i < Reg_MAX; i++)
        result.push_back(i);
    return result;
}

BNIntrinsicClass YSCArchitecture::GetIntrinsicClass(uint32_t intrinsic)
{
    return BNIntrinsicClass::GeneralIntrinsicClass;
}

std::string YSCArchitecture::GetIntrinsicName(uint32_t intrinsic)
{
    if (intrinsic >= Intrin_MAX)
    {
        return "UNKINTRIN";
    }

    return std::string(g_intrinNames[intrinsic]);
}

std::vector<uint32_t> YSCArchitecture::GetAllIntrinsics()
{
    std::vector<uint32_t> result;
    for (uint32_t i = 0; i < Intrin_MAX; i++)
        result.push_back(i);
    return result;
}

std::vector<BinaryNinja::NameAndType> YSCArchitecture::GetIntrinsicInputs(uint32_t intrinsic)
{
    using namespace BinaryNinja;
    std::vector<NameAndType> result;
    auto charPtr = Type::PointerType(this, Type::IntegerType(1, true, "char"), false);
    auto constCharPtr = Type::PointerType(this, Type::IntegerType(1, true, "char"), true);
    auto constVoidPtr = Type::PointerType(this, Type::VoidType(), true);
    auto int32 = Type::IntegerType(4, true);
    auto uint32 = Type::IntegerType(4, false);
    switch (intrinsic)
    {
    case Intrin_StringHash:
        result.emplace_back("str", Type::PointerType(4, Type::IntegerType(1, false)));
        break;
    case Intrin_TextLabelAssignString:
    case Intrin_TextLabelAppendString:
        result.emplace_back("dst", charPtr);
        result.emplace_back("src", constCharPtr);
        result.emplace_back("size", uint32);
        break;
    case Intrin_TextLabelAssignInt:
    case Intrin_TextLabelAppendInt:
        result.emplace_back("dst", charPtr);
        result.emplace_back("value", int32);
        result.emplace_back("size", uint32);
        break;
    case Intrin_TextLabelCopy:
        result.emplace_back("dst", charPtr);
        result.emplace_back("repeat", uint32);
        result.emplace_back("src", constVoidPtr);
        break;
    default:
        break;
    }
    return result;
}

std::vector<BinaryNinja::Confidence<BinaryNinja::Ref<BinaryNinja::Type>>>
YSCArchitecture::GetIntrinsicOutputs(uint32_t intrinsic)
{
    using namespace BinaryNinja;
    std::vector<Confidence<Ref<Type>>> result;
    switch (intrinsic)
    {
    case Intrin_StringHash:
        result.emplace_back(Type::IntegerType(4, false));
        break;
    case Intrin_TextLabelAssignString:
    case Intrin_TextLabelAssignInt:
    case Intrin_TextLabelAppendString:
    case Intrin_TextLabelAppendInt:
    case Intrin_TextLabelCopy:
        break;
    default:
        break;
    }
    return result;
}

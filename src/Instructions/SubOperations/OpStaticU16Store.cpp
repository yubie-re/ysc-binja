#include "inc.hpp"
#include "OpStaticU16Store.hpp"

size_t OpStaticU16Store::GetSize()
{
    return 3;
}

std::string_view OpStaticU16Store::GetName()
{
    return "STATIC_U16_STORE";
}

void OpStaticU16Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU16Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    if(!il.GetFunction())
        return false;
    if(!il.GetFunction()->GetView())
        return false;
    if(!il.GetFunction()->GetView()->GetSectionByName("STATICS"))
        return false;

    il.AddInstruction(il.Store(4, il.Const(4, 8 * static_cast<int>(operand) + il.GetFunction()->GetView()->GetSectionByName("STATICS")->GetStart()), il.Pop(4)));
    return true;
}

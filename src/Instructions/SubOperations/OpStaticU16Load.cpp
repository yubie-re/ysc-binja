#include "inc.hpp"
#include "OpStaticU16Load.hpp"

size_t OpStaticU16Load::GetSize()
{
    return 3;
}

std::string_view OpStaticU16Load::GetName()
{
    return "STATIC_U16_LOAD";
}

void OpStaticU16Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU16Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    if(!il.GetFunction())
        return false;
    if(!il.GetFunction()->GetView())
        return false;
    if(!il.GetFunction()->GetView()->GetSectionByName("STATICS"))
        return false;

    il.AddInstruction(il.Push(4, il.Load(4, il.Const(4, 8 * static_cast<int>(operand) + il.GetFunction()->GetView()->GetSectionByName("STATICS")->GetStart()))));
    return true;
}

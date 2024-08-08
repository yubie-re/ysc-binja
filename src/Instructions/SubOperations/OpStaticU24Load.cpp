#include "inc.hpp"
#include "OpStaticU24Load.hpp"
#include "Uint24.hpp"

size_t OpStaticU24Load::GetSize()
{
    return 4;
}

std::string_view OpStaticU24Load::GetName()
{
    return "STATIC_U24_LOAD";
}

void OpStaticU24Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU24Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    if(!il.GetFunction())
        return false;
    if(!il.GetFunction()->GetView())
        return false;
    if(!il.GetFunction()->GetView()->GetSectionByName("STATICS"))
        return false;

    il.AddInstruction(il.Push(4, il.Load(4, il.Add(4, il.Const(4, 8 * operand), il.Const(4, il.GetFunction()->GetView()->GetSectionByName("STATICS")->GetStart())))));
    return true;
}

#include "inc.hpp"
#include "OpIoffsetS16Load.hpp"

size_t OpIoffsetS16Load::GetSize()
{
    return 3;
}

std::string_view OpIoffsetS16Load::GetName()
{
    return "IOFFSET_S16_LOAD";
}

void OpIoffsetS16Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpIoffsetS16Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    il.AddInstruction(il.Push(4, il.Load(4, il.Add(4, il.Pop(4), il.Const(4, static_cast<int>(operand) * 8)))));
    return true;
}

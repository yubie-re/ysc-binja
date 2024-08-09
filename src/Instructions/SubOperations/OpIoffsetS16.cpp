#include "inc.hpp"
#include "OpIoffsetS16.hpp"

size_t OpIoffsetS16::GetSize()
{
    return 3;
}

std::string_view OpIoffsetS16::GetName()
{
    return "IOFFSET_S16";
}

void OpIoffsetS16::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpIoffsetS16::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    il.AddInstruction(il.Push(4, il.Add(4, il.Pop(4), il.Const(4, static_cast<int>(operand) * 8))));
    return true;
}

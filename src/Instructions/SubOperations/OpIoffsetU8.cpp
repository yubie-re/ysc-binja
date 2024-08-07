#include "inc.hpp"
#include "OpIoffsetU8.hpp"

size_t OpIoffsetU8::GetSize()
{
    return 2;
}

std::string_view OpIoffsetU8::GetName()
{
    return "IOFFSET_U8";
}

void OpIoffsetU8::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpIoffsetU8::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    il.AddInstruction(il.Push(4, il.Add(4, il.Pop(4), il.Const(4, operand))));
    return true;
}

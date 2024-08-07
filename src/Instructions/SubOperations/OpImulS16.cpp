#include "inc.hpp"
#include "OpImulS16.hpp"

size_t OpImulS16::GetSize()
{
    return 3;
}

std::string_view OpImulS16::GetName()
{
    return "IMUL_S16";
}

void OpImulS16::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpImulS16::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int16_t operand = *reinterpret_cast<const uint16_t*>(data);
    il.AddInstruction(il.Push(4, il.Mult(4, il.Pop(4), il.Const(4, operand))));
    return true;
}

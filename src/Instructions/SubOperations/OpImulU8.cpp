#include "inc.hpp"
#include "OpImulU8.hpp"

size_t OpImulU8::GetSize()
{
    return 2;
}

std::string_view OpImulU8::GetName()
{
    return "IMUL_U8";
}

void OpImulU8::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", data[0]), data[0]));
}

bool OpImulU8::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Mult(4, il.Pop(4), il.Const(4, data[0]))));
    return true;
}
#include "inc.hpp"
#include "OpPushConstU8U8.hpp"

size_t OpPushConstU8U8::GetSize()
{
    return 3;
}

std::string_view OpPushConstU8U8::GetName()
{
    return "PUSH_CONST_U8_U8";
}

void OpPushConstU8U8::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    uint8_t operandOne = data[0];
    uint8_t operandTwo = data[1];
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", operandOne), operandOne));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, ", "));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", operandTwo), operandTwo));
}

bool OpPushConstU8U8::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    uint8_t operandOne = data[0];
    uint8_t operandTwo = data[1];
    il.AddInstruction(il.Push(4, il.Const(4, operandOne)));
    il.AddInstruction(il.Push(4, il.Const(4, operandTwo)));
    return true;
}

#include "inc.hpp"
#include "OpPushConstF.hpp"

size_t OpPushConstF::GetSize()
{
    return 5;
}

std::string_view OpPushConstF::GetName()
{
    return "PUSH_CONST_FLOAT";
}

void OpPushConstF::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const float operand = *reinterpret_cast<const float*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{}", operand)));
}

bool OpPushConstF::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const float operand = *reinterpret_cast<const float*>(data);
    il.AddInstruction(il.Push(4, il.FloatConstSingle(operand)));
    return true;
}
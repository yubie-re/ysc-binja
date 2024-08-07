#include "inc.hpp"
#include "OpPushConstU8.hpp"

size_t OpPushConstU8::GetSize()
{
    return 2;
}

std::string_view OpPushConstU8::GetName()
{
    return "PUSH_CONST_U8";
}

void OpPushConstU8::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpPushConstU8::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    il.AddInstruction(il.Push(4, il.Const(4, operand)));
    return true;
}

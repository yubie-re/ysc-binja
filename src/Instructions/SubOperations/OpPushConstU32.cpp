#include "inc.hpp"
#include "OpPushConstU32.hpp"

size_t OpPushConstU32::GetSize()
{
    return 5;
}

std::string_view OpPushConstU32::GetName()
{
    return "PUSH_CONST_U32";
}

void OpPushConstU32::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = *reinterpret_cast<const uint32_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpPushConstU32::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = *reinterpret_cast<const uint32_t*>(data);
    il.AddInstruction(il.Push(4, il.Const(4, operand)));
    return true;
}

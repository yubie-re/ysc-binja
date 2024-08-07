#include "inc.hpp"
#include "OpPushConstS16.hpp"

size_t OpPushConstS16::GetSize()
{
    return 3;
}

std::string_view OpPushConstS16::GetName()
{
    return "PUSH_CONST_S16";
}

void OpPushConstS16::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpPushConstS16::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    il.AddInstruction(il.Push(4, il.Const(4, operand)));
    return true;
}

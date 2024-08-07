#include "inc.hpp"
#include "OpPushConstU24.hpp"
#include "Uint24.hpp"

size_t OpPushConstU24::GetSize()
{
    return 4;
}

std::string_view OpPushConstU24::GetName()
{
    return "PUSH_CONST_U24";
}

void OpPushConstU24::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpPushConstU24::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    il.AddInstruction(il.Push(4, il.Const(4, operand)));
    return true;
}

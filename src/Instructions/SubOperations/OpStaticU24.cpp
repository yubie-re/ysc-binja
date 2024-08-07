#include "inc.hpp"
#include "OpStaticU24.hpp"
#include "Uint24.hpp"

size_t OpStaticU24::GetSize()
{
    return 4;
}

std::string_view OpStaticU24::GetName()
{
    return "STATIC_U24";
}

void OpStaticU24::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU24::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    il.AddInstruction(il.Push(4, il.ConstPointer(4, operand + STACK_VADDR)));
    return true;
}

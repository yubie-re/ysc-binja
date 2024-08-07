#include "inc.hpp"
#include "OpGlobalU24.hpp"
#include "Uint24.hpp"

size_t OpGlobalU24::GetSize()
{
    return 4;
}

std::string_view OpGlobalU24::GetName()
{
    return "GLOBAL_U24";
}

void OpGlobalU24::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpGlobalU24::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    il.AddInstruction(il.Push(4, il.Load(4, il.Const(4, operand + GLOBAL_VADDR))));
    return true;
}

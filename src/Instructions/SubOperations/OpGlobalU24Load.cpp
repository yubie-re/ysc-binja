#include "inc.hpp"
#include "OpGlobalU24Load.hpp"
#include "Uint24.hpp"

size_t OpGlobalU24Load::GetSize()
{
    return 4;
}

std::string_view OpGlobalU24Load::GetName()
{
    return "GLOBAL_U24_LOAD";
}

void OpGlobalU24Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpGlobalU24Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    il.AddInstruction(il.Push(4, il.Load(4, il.Const(4, operand + GLOBAL_VADDR))));
    return true;
}

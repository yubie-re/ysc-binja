#include "inc.hpp"
#include "OpGlobalU24Store.hpp"
#include "Uint24.hpp"

size_t OpGlobalU24Store::GetSize()
{
    return 4;
}

std::string_view OpGlobalU24Store::GetName()
{
    return "GLOBAL_U24_STORE";
}

void OpGlobalU24Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpGlobalU24Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    il.AddInstruction(il.Store(4, il.Const(4, operand + GLOBAL_VADDR), il.Pop(4)));
    return true;
}

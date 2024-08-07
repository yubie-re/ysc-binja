#include "inc.hpp"
#include "OpGlobalU16Load.hpp"

size_t OpGlobalU16Load::GetSize()
{
    return 3;
}

std::string_view OpGlobalU16Load::GetName()
{
    return "GLOBAL_U16_LOAD";
}

void OpGlobalU16Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpGlobalU16Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    il.AddInstruction(il.Push(4, il.Load(4, il.Const(4, operand + GLOBAL_VADDR))));
    return true;
}

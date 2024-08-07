#include "inc.hpp"
#include "OpGlobalU16.hpp"

size_t OpGlobalU16::GetSize()
{
    return 3;
}

std::string_view OpGlobalU16::GetName()
{
    return "GLOBAL_U16";
}

void OpGlobalU16::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpGlobalU16::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    il.AddInstruction(il.Push(4, il.Load(4, il.Const(4, operand + GLOBAL_VADDR))));
    return true;
}

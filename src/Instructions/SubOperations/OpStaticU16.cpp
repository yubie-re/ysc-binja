#include "inc.hpp"
#include "OpStaticU16.hpp"

size_t OpStaticU16::GetSize()
{
    return 3;
}

std::string_view OpStaticU16::GetName()
{
    return "STATIC_U16";
}

void OpStaticU16::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU16::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    il.AddInstruction(il.Push(4, il.ConstPointer(4, operand + STACK_VADDR)));
    return true;
}

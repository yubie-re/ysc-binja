#include "inc.hpp"
#include "OpStaticU8Load.hpp"

size_t OpStaticU8Load::GetSize()
{
    return 2;
}

std::string_view OpStaticU8Load::GetName()
{
    return "STATIC_U8_LOAD";
}

void OpStaticU8Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = data[0];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU8Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t operand = data[0];
    il.AddInstruction(il.Push(4, il.Load(4, il.Add(4, il.Const(4, operand), il.Const(4, STACK_VADDR)))));
    return true;
}
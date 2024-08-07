#include "inc.hpp"
#include "OpStaticU8.hpp"

size_t OpStaticU8::GetSize()
{
    return 2;
}

std::string_view OpStaticU8::GetName()
{
    return "STATIC_U8";
}

void OpStaticU8::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = data[0];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU8::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.ConstPointer(4, il.Add(4, il.Const(4, data[0]), il.Const(4, STACK_VADDR)))));
    return true;
}

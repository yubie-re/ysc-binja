#include "inc.hpp"
#include "OpStaticU8Store.hpp"

size_t OpStaticU8Store::GetSize()
{
    return 2;
}

std::string_view OpStaticU8Store::GetName()
{
    return "STATIC_U8_STORE";
}

void OpStaticU8Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = data[0];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU8Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Store(4, il.Add(4, il.Const(4, data[0]), il.Const(4, STACK_VADDR)), il.Pop(4)));
    return true;
}

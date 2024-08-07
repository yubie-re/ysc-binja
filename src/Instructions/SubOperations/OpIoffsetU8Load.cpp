#include "inc.hpp"
#include "OpIoffsetU8Load.hpp"

size_t OpIoffsetU8Load::GetSize()
{
    return 2;
}

std::string_view OpIoffsetU8Load::GetName()
{
    return "IOFFSET_U8_LOAD";
}

void OpIoffsetU8Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpIoffsetU8Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    il.AddInstruction(il.Push(4, il.Load(4, il.Add(4, il.Pop(4), il.Const(4, operand)))));
    return true;
}

#include "inc.hpp"
#include "OpLocalU16Load.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpLocalU16Load::GetSize()
{
    return 3;
}

std::string_view OpLocalU16Load::GetName()
{
    return "LOCAL_U16_LOAD";
}

void OpLocalU16Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpLocalU16Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    return il.AddInstruction(il.Push(4, il.Load(4, il.Add(4, il.Const(4,operand), il.Register(4, Reg_FP)))));
}

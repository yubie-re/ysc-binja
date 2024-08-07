#include "inc.hpp"
#include "OpLocalU16.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpLocalU16::GetSize()
{
    return 3;
}

std::string_view OpLocalU16::GetName()
{
    return "LOCAL_U16";
}

void OpLocalU16::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpLocalU16::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    il.AddInstruction(il.Push(4, il.ConstPointer(4, il.Add(4, il.Const(4, operand), il.Register(4, Reg_FP)))));
    return true;
}

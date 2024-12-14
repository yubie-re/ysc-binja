#include "inc.hpp"
#include "OpLocalU8Load.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpLocalU8Load::GetSize()
{
    return 2;
}

std::string_view OpLocalU8Load::GetName()
{
    return "LOCAL_U8_LOAD";
}

void OpLocalU8Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpLocalU8Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    il.AddInstruction(il.Push(4, il.Load(4, il.Sub(4, il.Register(4, Reg_FP), il.Const(4,operand * 4)))));
    return true;
}
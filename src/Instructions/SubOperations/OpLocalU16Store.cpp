#include "inc.hpp"
#include "OpLocalU16Store.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpLocalU16Store::GetSize()
{
    return 3;
}

std::string_view OpLocalU16Store::GetName()
{
    return "LOCAL_U16_STORE";
}

void OpLocalU16Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpLocalU16Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    il.AddInstruction(il.Store(4, il.Add(4, il.Const(4,operand * 4), il.Register(4, Reg_FP)), il.Pop(4)));
    return true;
}

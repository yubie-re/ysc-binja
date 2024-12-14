#include "inc.hpp"
#include "OpLocalU8Store.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpLocalU8Store::GetSize()
{
    return 2;
}

std::string_view OpLocalU8Store::GetName()
{
    return "LOCAL_U8_STORE";
}

void OpLocalU8Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpLocalU8Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    il.AddInstruction(il.Store(4, il.Sub(4, il.Register(4, Reg_FP), il.Const(4,operand * 4)), il.Pop(4)));
    return true;
}

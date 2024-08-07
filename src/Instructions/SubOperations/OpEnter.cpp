#include "inc.hpp"
#include "OpEnter.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpEnter::GetSize()
{
    return 5;
}

std::string_view OpEnter::GetName()
{
    return "ENTER";
}

void OpEnter::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t paramCount = data[0];
    const uint16_t localCount = *reinterpret_cast<const uint16_t*>(&data[1]);
    const uint8_t nameCount = data[3];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", paramCount), paramCount));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, ", "));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", localCount), localCount));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, ", "));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", nameCount), nameCount));
}

bool OpEnter::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t paramCount = data[0];
    const uint16_t localCount = *reinterpret_cast<const uint16_t*>(&data[1]);
    const uint8_t nameCount = data[3];
    il.AddInstruction(il.Push(4, il.Register(4, Reg_FP)));
    il.AddInstruction(il.SetRegister(4, Reg_FP, il.Sub(4, il.Register(4, Reg_SP), il.Const(4, (paramCount + 1) * 4))));
    for(int i = 0; i < localCount; i++)
        il.AddInstruction(il.Push(4, il.Const(4, 0)));
    for(int i = 0; i < paramCount; i++)
        il.AddInstruction(il.Pop(4));
    return true;
}

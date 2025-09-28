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
    const uint8_t paramCount = GetOperand<OpU8>(data, len, 0).ToValue();
    const uint16_t localCount = GetOperand<OpU16>(data, len, 1).ToValue();
    const uint8_t nameCount = GetOperand<OpU8>(data, len, 3).ToValue();//always 0
    len = GetSize() + nameCount;
    
    std::string_view name(reinterpret_cast<const char*>(data + 4), nameCount);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", paramCount), paramCount));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, ", "));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", localCount), localCount));
    if(nameCount > 0)
        result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, ", "));
    if(nameCount > 0)
        result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::StringToken, fmt::format("\"{}\"", name), 0));
}

bool OpEnter::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t paramCount = GetOperand<OpU8>(data, len, 0).ToValue();
    const uint16_t localCount = GetOperand<OpU16>(data, len, 1).ToValue();
    const uint8_t nameCount = GetOperand<OpU8>(data, len, 3).ToValue();//always 0
    len += nameCount;
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Const(4, 0)));
    il.AddInstruction(il.Push(4, il.Register(4, Reg_FP))); // --sp = FP
    il.AddInstruction(il.SetRegister(4, Reg_FP, il.Add(4, il.Register(4, Reg_SP), il.Const(4, (paramCount) * 4)))); // fp = sp + paramCount (pop off params)
    for(int i = 0; i < localCount; i++)
        il.AddInstruction(il.Push(4, il.Const(4, 0))); // --sp = 0
    il.AddInstruction(il.SetRegister(4, Reg_SP, il.Add(4, il.Register(4, Reg_SP), il.Const(4, 4 * paramCount)))); // sp += paramCount
    return true;
}

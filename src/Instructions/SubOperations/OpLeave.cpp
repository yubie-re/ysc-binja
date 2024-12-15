#include "inc.hpp"
#include "OpLeave.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpLeave::GetSize()
{
    return 3;
}

std::string_view OpLeave::GetName()
{
    return "LEAVE";
}

void OpLeave::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    uint8_t paramCount = data[0];
    uint8_t returnSize = data[1];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", paramCount), paramCount));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, ", "));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", returnSize), returnSize));
}

bool OpLeave::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    uint8_t paramCount = data[0];
    uint8_t returnSize = data[1];

    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Add(4, il.Register(4, Reg_SP), il.Const(4, returnSize * 4)))); // return values
    il.AddInstruction(il.SetRegister(4, Reg_SP, il.Register(4, Reg_FP))); // Get OG SP back
    il.AddInstruction(il.SetRegister(4, Reg_FP, il.Pop(4))); // Restore OG FP
    il.AddInstruction(il.SetRegister(4, Reg_R2, il.Pop(4))); // Get return address, store in R2

    for(int i = returnSize; i > 0; i--)
        il.AddInstruction(il.Push(4, il.Load(4, il.Sub(4, il.Register(4, Reg_R1), il.Const(4, i * 4)))));

    if(il.GetFunction() && !il.GetFunction()->GetStart())
    {
        il.AddInstruction(il.NoReturn());
        return true;
    }
    il.AddInstruction(il.Return(il.Register(4, Reg_R2)));
    return true;
}


bool OpLeave::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    result.AddBranch(BNBranchType::FunctionReturn);
    return true;
}
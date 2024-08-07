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

    il.AddInstruction(il.SetRegister(4, Reg_TMP, il.Sub(4, il.Register(4, Reg_SP), il.Const(4, returnSize * 4))));
    il.AddInstruction(il.SetRegister(4, Reg_SP, il.Add(4, il.Register(4, Reg_FP), il.Const(4, (paramCount  + 1) * 4))));

    //  ssp = fp + paramCount + 1 = old fp
    //il.Pop(4) = sp = fp + paramCount = newPc (ret addr)
    //il.Pop(4) = sp = fp + paramCount - 1 = sp restore
    il.AddInstruction(il.SetRegister(4, Reg_FP, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_RETADDR, il.Pop(4)));

    for(int i = 0; i < paramCount; i++)
        il.AddInstruction(il.Pop(4));

    for(int i = 0; i < returnSize; i++)
        il.AddInstruction(il.Push(4, il.Load(4, il.Sub(4, il.Register(4, Reg_TMP), il.Const(4, 4 * i)))));

    if(il.GetFunction().GetPtr() && !il.GetFunction()->GetStart())
    {
        il.AddInstruction(il.NoReturn());
        return true;
    }

    il.AddInstruction(il.Return(il.Register(4, Reg_RETADDR)));
    return true;
}


bool OpLeave::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    result.AddBranch(BNBranchType::FunctionReturn);
    return true;
}
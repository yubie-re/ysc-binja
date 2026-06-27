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
    uint8_t returnSize = data[1];

    if(il.GetFunction() && !il.GetFunction()->GetStart())
    {
        il.AddInstruction(il.NoReturn());
        return true;
    }

    if (returnSize > 0)
        il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4)));

    il.AddInstruction(il.Return(il.ConstPointer(4, 0)));
    return true;
}


bool OpLeave::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    result.AddBranch(BNBranchType::FunctionReturn);
    return true;
}

bool OpLeave::GetInstructionBlockAnalysis(YSCBlockAnalysisContext& ctx, size_t address, size_t& bytesRead)
{
    std::vector<uint8_t> instr(GetSize());
    ctx.GetView()->Read(instr.data(), address, GetSize());
    ctx.GetCurrentBlock()->AddInstructionData(instr.data(), instr.size());
    bytesRead += GetSize();
    return true;
}

#include "inc.hpp"
#include "OperationBase.hpp"
#include "../Architecture/YSCArchitecture.hpp"

void OpBase::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len,
                                std::vector<BinaryNinja::InstructionTextToken>& result)
{
    result.push_back(
        BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::InstructionToken, std::string(GetName())));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, " "));
}

bool OpBase::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len,
                                      BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Unimplemented());
    return true;
}

bool OpBase::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    result.length = GetSize();
    return true;
}

bool OpBase::GetInstructionBlockAnalysis(YSCBlockAnalysisContext& ctx, size_t address, size_t& bytesRead)
{
    std::vector<uint8_t> instr(GetSize());
    ctx.GetView()->Read(instr.data(), address, GetSize());
    ctx.GetCurrentBlock()->AddInstructionData(instr.data(), instr.size());
    bytesRead += instr.size();
    return false;
}

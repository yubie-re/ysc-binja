#include "inc.hpp"
#include "OpThrow.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpThrow::GetSize()
{
    return 1;
}

std::string_view OpThrow::GetName()
{
    return "THROW";
}

bool OpThrow::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Nop());
    return true;
}

bool OpThrow::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    result.AddBranch(BNBranchType::UnresolvedBranch);
    return true;
}

bool OpThrow::GetInstructionBlockAnalysis(YSCBlockAnalysisContext& ctx, size_t address, size_t& bytesRead)
{
    std::vector<uint8_t> instr(GetSize());
    ctx.GetView()->Read(instr.data(), address, GetSize());
    ctx.GetCurrentBlock()->AddPendingOutgoingEdge(BNBranchType::UnresolvedBranch, address);
    ctx.GetCurrentBlock()->AddInstructionData(instr.data(), instr.size());
    bytesRead += GetSize();
    return true;
}

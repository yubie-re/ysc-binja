#include "inc.hpp"
#include "OpCall.hpp"
#include "Uint24.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpCall::GetSize()
{
    return 4;
}

std::string_view OpCall::GetName()
{
    return "CALL";
}

void OpCall::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len,
                                std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data) + CODE_OFFSET;
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::PossibleAddressToken,
                                                       fmt::format("{:x}", operand), operand));
}

bool OpCall::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len,
                                      BinaryNinja::LowLevelILFunction& il)
{
    if (!il.GetFunction())
        return false;
    if (!il.GetFunction()->GetView())
        return false;
    if (!il.GetFunction()->GetView()->GetSectionByName("CODE"))
        return false;
    const uint32_t operand = Uint24(data) + il.GetFunction()->GetView()->GetSectionByName("CODE")->GetStart();
    il.AddInstruction(il.Call(il.Const(4, operand)));
    return true;
}

bool OpCall::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    const uint32_t operand = Uint24(data) + CODE_OFFSET;
    result.AddBranch(BNBranchType::CallDestination, operand);
    return true;
}

bool OpCall::GetInstructionBlockAnalysis(YSCBlockAnalysisContext& ctx, size_t address, size_t& bytesRead)
{
    std::vector<uint8_t> instr(GetSize());
    ctx.GetView()->Read(instr.data(), address, GetSize());
    ctx.GetCurrentBlock()->AddPendingOutgoingEdge(BNBranchType::CallDestination,
                                                  GetOperand<OpU24>(instr, 1).ToValue() + CODE_OFFSET);
    return OpBase::GetInstructionBlockAnalysis(ctx, address, bytesRead);
}
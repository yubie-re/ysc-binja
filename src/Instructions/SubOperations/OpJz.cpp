#include "inc.hpp"
#include "OpJz.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpJz::GetSize()
{
    return 3;
}

std::string_view OpJz::GetName()
{
    return "JZ";
}

void OpJz::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const int32_t operand = static_cast<int32_t>(addr) + static_cast<int32_t>(*reinterpret_cast<const int16_t*>(data)) + 3;
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::PossibleAddressToken, fmt::format("{:x}", operand), operand));
}

bool OpJz::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int32_t operand = static_cast<int32_t>(addr) + static_cast<int32_t>(*reinterpret_cast<const int16_t*>(data)) + 3;
    BinaryNinja::LowLevelILLabel b1;
    BinaryNinja::LowLevelILLabel b2;
    il.AddInstruction(il.If(il.Pop(4), b1, b2));
    il.MarkLabel(b2);
    auto branchIlLabelPtr = il.GetLabelForAddress(BinaryNinja::Architecture::GetByName("YSC"), operand);
    if(branchIlLabelPtr)
        il.AddInstruction(il.Goto(*branchIlLabelPtr));
    else
        il.AddInstruction(il.Jump(il.Const(4, operand)));
    il.MarkLabel(b1);
    return true;
}

bool OpJz::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    const int32_t operand = static_cast<int32_t>(addr) + static_cast<int32_t>(*reinterpret_cast<const int16_t*>(data)) + 3;
    result.AddBranch(BNBranchType::TrueBranch, addr + 3);
    result.AddBranch(BNBranchType::FalseBranch, operand);
    return true;
}

bool OpJz::GetInstructionBlockAnalysis(YSCBlockAnalysisContext& ctx, size_t address, size_t& bytesRead)
{
    std::vector<uint8_t> instr(GetSize());
    ctx.GetView()->Read(instr.data(), address, GetSize());
    size_t jmpAddress = GetOperand<OpU16>(instr, 1).ToValue() + address + 3;

    ctx.GetCurrentBlock()->AddPendingOutgoingEdge(BNBranchType::TrueBranch, address + GetSize());
    ctx.GetCurrentBlock()->AddPendingOutgoingEdge(BNBranchType::FalseBranch, jmpAddress);
    ctx.QueueAddress(address + GetSize());
    ctx.QueueAddress(jmpAddress);
    ctx.GetCurrentBlock()->AddInstructionData(instr.data(), instr.size());
    bytesRead += GetSize();
    return true;
}
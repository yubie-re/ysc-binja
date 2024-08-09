#include "inc.hpp"
#include "OpIeqJz.hpp"

size_t OpIeqJz::GetSize()
{
    return 3;
}

std::string_view OpIeqJz::GetName()
{
    return "IEQ_JZ";
}

void OpIeqJz::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const int32_t operand = static_cast<int32_t>(addr) + static_cast<int32_t>(*reinterpret_cast<const int16_t*>(data)) + 3;
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::PossibleAddressToken, fmt::format("{:x}", operand), operand));
}

bool OpIeqJz::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int32_t operand = static_cast<int32_t>(addr) + static_cast<int32_t>(*reinterpret_cast<const int16_t*>(data)) + 3;
    auto t = BinaryNinja::LowLevelILLabel();
    auto f = BinaryNinja::LowLevelILLabel();
    il.AddInstruction(il.If(il.CompareEqual(4, il.Pop(4), il.Pop(4)), t, f));
    il.MarkLabel(f);
    auto branchIlLabelPtr = il.GetLabelForAddress(BinaryNinja::Architecture::GetByName("YSC"), operand);
    if(branchIlLabelPtr)
        il.AddInstruction(il.Goto(*branchIlLabelPtr));
    else
        il.AddInstruction(il.Jump(il.Const(4, operand)));
    il.MarkLabel(t);
    return true;
}

bool OpIeqJz::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    const int32_t operand = static_cast<int32_t>(addr) + static_cast<int32_t>(*reinterpret_cast<const int16_t*>(data)) + 3;
    result.AddBranch(BNBranchType::TrueBranch, addr + 3);
    result.AddBranch(BNBranchType::FalseBranch, operand);
    return true;
}

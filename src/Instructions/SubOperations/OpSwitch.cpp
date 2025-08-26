#include "inc.hpp"
#include "OpSwitch.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpSwitch::GetSize()
{
    return 2;
}

std::string_view OpSwitch::GetName()
{
    return "SWITCH";
}

void OpSwitch::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t switchCount = data[0];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", switchCount), switchCount));
}

bool OpSwitch::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t switchCount = data[0];

    if(!il.GetFunction())
        return false;
    if(!il.GetFunction()->GetView())
        return false;

    il.AddInstruction(il.SetRegister(4, Reg_SWITCH, il.Pop(4)));
    std::vector<SwitchCase> switchData(static_cast<int>(switchCount));
    il.GetFunction()->GetView()->Read(switchData.data(), addr + 2, sizeof(SwitchCase) * static_cast<int>(switchCount));
    //il.AddInstruction(il.SetRegister(4, Reg_SWITCH, il.Pop(4)));
    ProcessSwitchCases(switchData, il, switchCount, addr);
    return true;
}

void OpSwitch::ProcessSwitchCases(std::vector<SwitchCase> switchData, BinaryNinja::LowLevelILFunction& il, int switchCount, uint64_t address, int index)
{
    if (index >= switchCount) // Base case: if index reaches the switchCount, stop recursion
    {
        auto branchIlLabelPtr = il.GetLabelForAddress(BinaryNinja::Architecture::GetByName("YSC"), address + index * 6 + 2);
        if(branchIlLabelPtr)
            il.AddInstruction(il.Goto(*branchIlLabelPtr));
        else
            il.AddInstruction(il.Jump(il.Const(4, address + index * 6 + 2)));
        return;
    }

    SwitchCase switchCase = switchData[index];
    auto branchIlLabelPtr = il.GetLabelForAddress(BinaryNinja::Architecture::GetByName("YSC"), address + static_cast<int>(switchCase.m_target) + (index + 1) * 6 + 2);
    BinaryNinja::LowLevelILLabel t;
    BinaryNinja::LowLevelILLabel f;
    il.AddInstruction(
        il.If(
            il.CompareEqual(4,
                            il.Const(4, switchCase.m_case),
                            il.Register(4, Reg_SWITCH)),
            t, f)
    );
    il.MarkLabel(t);
    if(branchIlLabelPtr)
        il.AddInstruction(il.Goto(*branchIlLabelPtr));
    else
        il.AddInstruction(il.Jump(il.Const(4, address + static_cast<int>(switchCase.m_target) + (index + 1) * 6 + 2)));
    il.MarkLabel(f);

    // Recursive call to process the next switch case
    ProcessSwitchCases(switchData, il, switchCount, address, index + 1);
}

bool OpSwitch::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::Ref<BinaryNinja::BasicBlock> block)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    result.AddBranch(BNBranchType::IndirectBranch);
    return true;
}
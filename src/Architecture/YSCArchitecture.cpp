#include "inc.hpp"
#include "YSCArchitecture.hpp"

std::string YSCArchitecture::GetRegisterName(uint32_t reg)
{
    if(reg >= Reg_MAX)
        return "UNKREG";
    return std::string(g_RegNames[reg]);
}

bool YSCArchitecture::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{ 
    uint8_t insn = data[0];
    if(insn >= OP_MAX)
        return false;
    m_insns[insn]->GetInstructionInfo(data + 1, addr, maxLen, result);
    return true;
}

bool YSCArchitecture::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    uint8_t insn = data[0];
    if(insn >= OP_MAX)
        return false;
    if(len < m_insns[insn]->GetSize())
        return false;
    len = m_insns[insn]->GetSize();
    m_insns[insn]->GetInstructionText(data + 1, addr, len, result);
    return true;
}

bool YSCArchitecture::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    uint8_t insn = data[0];
    if(insn >= OP_MAX)
        return false;
    if(len < m_insns[insn]->GetSize())
        return false;
    len = m_insns[insn]->GetSize();
    m_insns[insn]->GetInstructionLowLevelIL(data + 1, addr, len, il);
    return true;
}

BNRegisterInfo YSCArchitecture::GetRegisterInfo(uint32_t reg)
{
    BNRegisterInfo info;
    info.size = 4;
    return info;
}

uint32_t YSCArchitecture::GetStackPointerRegister()
{
    return Reg_SP;
}

std::vector<uint32_t> YSCArchitecture::GetAllRegisters() {
    std::vector<uint32_t> result;
    auto view = std::views::iota(0, Reg_MAX - 1);
    result.assign(view.begin(), view.end());
    return result;
}
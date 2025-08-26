#include "inc.hpp"
#include "YSCArchitecture.hpp"
#include <queue>

std::string YSCArchitecture::GetRegisterName(uint32_t reg)
{
    if (reg >= Reg_MAX)
        return "UNKREG";
    return std::string(g_RegNames[reg]);
}

/*
bool YSCArchitecture::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen,
                                         BinaryNinja::Ref<BinaryNinja::BasicBlock> block)
{
    uint8_t insn = data[0];
    if (insn >= OP_MAX)
        return false;
    m_insns[insn]->GetInstructionInfo(data + 1, addr, maxLen, result);
    return true;
}
*/

bool YSCArchitecture::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len,
                                         std::vector<BinaryNinja::InstructionTextToken>& result)
{
    uint8_t insn = data[0];
    if (insn >= OP_MAX)
        return false;
    if (len < m_insns[insn]->GetSize())
        return false;
    len = m_insns[insn]->GetSize();
    m_insns[insn]->GetInstructionText(data + 1, addr, len, result);
    return true;
}

bool YSCArchitecture::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len,
                                               BinaryNinja::LowLevelILFunction& il)
{
    uint8_t insn = data[0];
    if (insn >= OP_MAX)
        return false;
    if (len < m_insns[insn]->GetSize())
        return false;

    m_insns[insn]->GetInstructionLowLevelIL(data + 1, addr, len, il);
    if (!m_insns[insn]->CustomLLILSize())
        len = m_insns[insn]->GetSize();
    return true;
}

BNRegisterInfo YSCArchitecture::GetRegisterInfo(uint32_t reg)
{
    BNRegisterInfo info;
    if (reg < Reg_MAX)
        info.fullWidthRegister = reg;
    info.size = 4;
    info.extend = NoExtend;
    info.offset = 0;
    return info;
}

uint32_t YSCArchitecture::GetStackPointerRegister()
{
    return Reg_SP;
}

std::vector<uint32_t> YSCArchitecture::GetAllRegisters()
{
    return std::vector<uint32_t>(std::views::iota(0, Reg_MAX - 1).begin(), std::views::iota(0, Reg_MAX - 1).end());
}

BNIntrinsicClass YSCArchitecture::GetIntrinsicClass(uint32_t intrinsic)
{
    return BNIntrinsicClass::GeneralIntrinsicClass;
}

std::string YSCArchitecture::GetIntrinsicName(uint32_t intrinsic)
{
    if (intrinsic >= Intrin_MAX)
    {
        return "UNKINTRIN";
    }

    return std::string(g_intrinNames[intrinsic]);
}

std::vector<uint32_t> YSCArchitecture::GetAllIntrinsics()
{
    std::vector<uint32_t> result;
    auto view = std::views::iota(0, Intrin_MAX - 1);
    result.assign(view.begin(), view.end());
    return result;
}

std::vector<BinaryNinja::NameAndType> YSCArchitecture::GetIntrinsicInputs(uint32_t intrinsic)
{
    using namespace BinaryNinja;
    std::vector<NameAndType> result;
    switch (intrinsic)
    {
    default:
        break;
    }
    return result;
}

std::vector<BinaryNinja::Confidence<BinaryNinja::Ref<BinaryNinja::Type>>>
YSCArchitecture::GetIntrinsicOutputs(uint32_t intrinsic)
{
    using namespace BinaryNinja;
    std::vector<Confidence<Ref<Type>>> result;
    switch (intrinsic)
    {
    default:
        break;
    }
    return result;
}

void YSCArchitecture::AnalyzeBasicBlocks(BinaryNinja::Function* function, BinaryNinja::BasicBlockAnalysisContext& ctx)
{
    BinaryNinja::Ref<BinaryNinja::BinaryView> view = function->GetView();
    std::queue<uint64_t> blocksToProcess;
    blocksToProcess.push(function->GetStart());
    std::unordered_set<uint64_t> seenBlocks;
    std::unordered_map<uint64_t, BinaryNinja::Ref<BinaryNinja::BasicBlock>> blocks;
    while (!blocksToProcess.empty())
    {
        if (view->AnalysisIsAborted())
            break;
        uint64_t currentAddr = blocksToProcess.front();
        blocksToProcess.pop();
        if (seenBlocks.contains(currentAddr))
            continue;
        seenBlocks.insert(currentAddr);
        BinaryNinja::Ref<BinaryNinja::BasicBlock> block =
            ctx.CreateBasicBlock(function->GetArchitecture(), currentAddr);
        blocks[currentAddr] = block;
        bool endsBlock = false;
        while (true)
        {
            uint8_t insn;
            view->Read(&insn, currentAddr, 1);
            m_insns[insn]->GetSize();
            size_t insnSize = m_insns[insn]->GetSize();
            std::vector<uint8_t> data(insnSize);
            view->Read(data.data(), currentAddr, insnSize);
            
            if (insn >= OP_MAX)
                break;

            BinaryNinja::InstructionInfo result;
            m_insns[insn]->GetInstructionInfo(data.data() + 1, currentAddr, insnSize, block);
            
            
        }
    }
}

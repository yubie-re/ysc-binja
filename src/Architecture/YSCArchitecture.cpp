#include "inc.hpp"
#include "YSCArchitecture.hpp"
#include <queue>

std::string YSCArchitecture::GetRegisterName(uint32_t reg)
{
    if (reg >= Reg_MAX)
        return "UNKREG";
    return std::string(g_RegNames[reg]);
}


bool YSCArchitecture::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen,
                                         BinaryNinja::InstructionInfo& result)
{
    uint8_t insn = data[0];
    if (insn >= OP_MAX)
        return false;
    m_insns[insn]->GetInstructionInfo(data + 1, addr, maxLen, result);
    return true;
}


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

YSCBlockAnalysisContext::YSCBlockAnalysisContext(BinaryNinja::Function* function,
                                                 BinaryNinja::BasicBlockAnalysisContext* ctx)
    : m_function(function), m_ctx(ctx)
{
    m_blocksToProcess.push(function->GetStart());
}

/**
 * @brief Analyzes the basic blocks of a given function within the specified analysis context.
 */
void YSCArchitecture::AnalyzeBasicBlocks(BinaryNinja::Function* function, BinaryNinja::BasicBlockAnalysisContext& ctx)
{
    YSCBlockAnalysisContext analysisCtx(function, &ctx);
    if(!analysisCtx.IsFirstInstructionEnter())
    {
        ctx.Finalize();
    }
    while (analysisCtx.IsProcessing())
    {
        uint64_t currentAddr = analysisCtx.PopNextBlock();
        if (analysisCtx.HasSeenBlock(currentAddr))
            continue;
        analysisCtx.MarkBlockAsSeen(currentAddr);
        BinaryNinja::Ref<BinaryNinja::BasicBlock> block =
            ctx.CreateBasicBlock(function->GetArchitecture(), currentAddr);
        analysisCtx.AddBlock(currentAddr, block);
        while (true)
        {
            uint8_t insn;
            if (analysisCtx.GetView()->Read(&insn, currentAddr, 1) < 1)
                break;

            if(insn >= OP_MAX)
                break;

            size_t instrSize = m_insns[insn]->GetSize();
            if (insn >= OP_MAX)
                break;
            size_t bytesRead = 0;

            bool endsBlock = m_insns[insn]->GetInstructionBlockAnalysis(analysisCtx, currentAddr, bytesRead);

            if (bytesRead == 0)
                endsBlock = true;

            if (currentAddr + bytesRead > analysisCtx.GetView()->GetEnd()) // overflow
                endsBlock = true;

            currentAddr += bytesRead;

            if (endsBlock)
                break;
        }

        if (currentAddr != block->GetStart())
        {
            block->SetEnd(currentAddr);
            ctx.AddFunctionBasicBlock(block);
        }
    }

    ctx.Finalize();
}

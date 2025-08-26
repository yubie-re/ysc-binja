#ifndef YSC_ARCHITECTURE
#define YSC_ARCHITECTURE

#include "Instructions/OperationEnum.hpp"
#include "Instructions/OperationBase.hpp"
#include <array>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>

enum Registers
{
    Reg_SP,
    Reg_FP,
    Reg_SWITCH,
    Reg_VX1,
    Reg_VY1,
    Reg_VZ1,
    Reg_VX2,
    Reg_VY2,
    Reg_VZ2,
    Reg_R1,
    Reg_R2,
    Reg_R3,
    Reg_R4,
    Reg_MAX
};

const std::array<std::string_view, Reg_MAX> g_RegNames = {
    "SP", "FP", "SWITCH", "VX1", "VY1", "VZ1", "VX2", "VY2", "VZ2", "R1", "R2", "R3", "R4",
};

enum Intrin
{
    Intrin_MAX
};

const std::array<std::string_view, Intrin_MAX> g_intrinNames = {};

class YSCBlockAnalysisContext
{
  public:
    YSCBlockAnalysisContext(BinaryNinja::Function* function, BinaryNinja::BasicBlockAnalysisContext* ctx);

    BinaryNinja::Ref<BinaryNinja::BinaryView> GetView()
    {
        return m_function->GetView();
    }

    bool IsProcessing()
    {
        return !m_blocksToProcess.empty() && !GetView()->AnalysisIsAborted();
    }

    uint64_t PopNextBlock()
    {
        uint64_t addr = m_blocksToProcess.front();
        m_blocksToProcess.pop();
        return addr;
    }

    bool HasSeenBlock(uint64_t addr) const
    {
        return m_seenBlocks.contains(addr);
    }

    void MarkBlockAsSeen(uint64_t addr)
    {
        m_seenBlocks.insert(addr);
    }

    void AddBlock(uint64_t addr, BinaryNinja::Ref<BinaryNinja::BasicBlock> block)
    {
        m_blocks[addr] = block;
        m_currentBlock = block;
    }

    void QueueAddress(uint64_t addr)
    {
        m_blocksToProcess.push(addr);
    }

    BinaryNinja::Ref<BinaryNinja::BasicBlock> GetCurrentBlock()
    {
        return m_currentBlock;
    }

    bool IsFirstInstructionEnter()
    {
        uint8_t insn;
        GetView()->Read(&insn, m_blocksToProcess.front(), 1);
        return insn == OP_ENTER;
    }

  private:
    BinaryNinja::Function* m_function;
    std::queue<uint64_t> m_blocksToProcess;
    std::unordered_map<uint64_t, BinaryNinja::Ref<BinaryNinja::BasicBlock>> m_blocks;
    std::unordered_set<uint64_t> m_seenBlocks;
    BinaryNinja::BasicBlockAnalysisContext* m_ctx;
    bool m_shouldEndBlock = false;
    BinaryNinja::Ref<BinaryNinja::BasicBlock> m_currentBlock;
};

class YSCArchitecture : public BinaryNinja::Architecture
{
  public:
    YSCArchitecture(const std::string& name);

    BNEndianness GetEndianness() const override
    {
        return BNEndianness::LittleEndian;
    }

    size_t GetAddressSize() const override
    {
        return 4;
    };

    size_t GetInstructionAlignment() const override
    {
        return 1;
    };

    size_t GetDefaultIntegerSize() const override
    {
        return 4;
    };

    size_t GetMaxInstructionLength() const override
    {
        return 100;
    };

    std::string GetRegisterName(uint32_t reg) override;

    bool GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result) override;

    bool GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len,
                            std::vector<BinaryNinja::InstructionTextToken>& result) override;

    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len,
                                  BinaryNinja::LowLevelILFunction& il) override;

    BNRegisterInfo GetRegisterInfo(uint32_t reg) override;

    uint32_t GetStackPointerRegister() override;

    std::vector<uint32_t> GetAllRegisters() override;

    BNIntrinsicClass GetIntrinsicClass(uint32_t intrinsic) override;

    std::string GetIntrinsicName(uint32_t intrinsic) override;

    std::vector<uint32_t> GetAllIntrinsics() override;

    std::vector<BinaryNinja::NameAndType> GetIntrinsicInputs(uint32_t intrinsic) override;

    std::vector<BinaryNinja::Confidence<BinaryNinja::Ref<BinaryNinja::Type>>>
    GetIntrinsicOutputs(uint32_t intrinsic) override;

    void AnalyzeBasicBlocks(BinaryNinja::Function* function, BinaryNinja::BasicBlockAnalysisContext& context) override;

  private:
    std::array<std::unique_ptr<OpBase>, OP_MAX> m_insns;
};

#endif
#ifndef YSC_ARCHITECTURE
#define YSC_ARCHITECTURE

#include "Instructions/OperationEnum.hpp"
#include "Instructions/OperationBase.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

uintptr_t MarkYSCArchitectureViewActive(BinaryNinja::BinaryView* view);
void RetireYSCArchitectureView(uintptr_t key);

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
    Reg_ARG0,
    Reg_ARG1,
    Reg_ARG2,
    Reg_ARG3,
    Reg_ARG4,
    Reg_ARG5,
    Reg_ARG6,
    Reg_ARG7,
    Reg_ARG8,
    Reg_ARG9,
    Reg_ARG10,
    Reg_ARG11,
    Reg_ARG12,
    Reg_ARG13,
    Reg_ARG14,
    Reg_ARG15,
    Reg_MAX
};

const std::array<std::string_view, Reg_MAX> g_RegNames = {
    "SP", "FP", "SWITCH", "VX1", "VY1", "VZ1", "VX2", "VY2", "VZ2", "R1", "R2", "R3", "R4",
    "ARG0", "ARG1", "ARG2", "ARG3", "ARG4", "ARG5", "ARG6", "ARG7",
    "ARG8", "ARG9", "ARG10", "ARG11", "ARG12", "ARG13", "ARG14", "ARG15",
};

enum Intrin
{
    Intrin_StringHash,
    Intrin_TextLabelAssignString,
    Intrin_TextLabelAssignInt,
    Intrin_TextLabelAppendString,
    Intrin_TextLabelAppendInt,
    Intrin_TextLabelCopy,
    Intrin_MAX
};

const std::array<std::string_view, Intrin_MAX> g_intrinNames = {
    "ysc_string_hash",
    "ysc_text_label_assign_string",
    "ysc_text_label_assign_int",
    "ysc_text_label_append_string",
    "ysc_text_label_append_int",
    "ysc_text_label_copy",
};

bool EmitYSCTextLabelFallbackLLIL(uint8_t opcode, const uint8_t* data, size_t& len,
                                  BinaryNinja::LowLevelILFunction& il);

struct YSCSwitchCaseInfo
{
    uint32_t m_case = 0;
    uint64_t m_target = 0;
};

struct YSCSwitchInfo
{
    uint64_t m_address = 0;
    uint8_t m_caseCount = 0;
    uint64_t m_tableStart = 0;
    uint64_t m_tableEnd = 0;
    std::vector<YSCSwitchCaseInfo> m_cases;
};

struct YSCEnterInfo
{
    uint64_t m_address = 0;
    uint8_t m_paramCount = 0;
    uint16_t m_localCount = 0;
    uint8_t m_nameLength = 0;
    std::string m_name;
};

struct YSCFunctionContext
{
    uint64_t m_start = 0;
    std::optional<YSCEnterInfo> m_enter;
    std::optional<uint8_t> m_returnCount;
    std::map<uint64_t, YSCSwitchInfo> m_switches;
};

constexpr size_t YSC_MAX_INSTRUCTION_LENGTH = BN_MAX_INSTRUCTION_LENGTH;
constexpr size_t YSC_MAX_INTERNAL_INSTRUCTION_LENGTH = 2 + 255 * 6;

// Context class for managing the analysis of basic blocks in the YSC architecture.
class YSCBlockAnalysisContext
{
  public:
    // Constructor: initializes the context with the function and analysis context.
    YSCBlockAnalysisContext(BinaryNinja::Function* function, BinaryNinja::BasicBlockAnalysisContext* ctx);

    // Returns the BinaryView associated with the function being analyzed.
    BinaryNinja::Ref<BinaryNinja::BinaryView> GetView()
    {
        return m_function->GetView();
    }

    // Returns true if there are blocks left to process and analysis is not aborted.
    bool IsProcessing()
    {
        return !m_blocksToProcess.empty() && !GetView()->AnalysisIsAborted();
    }

    // Pops and returns the address of the next block to process.
    uint64_t PopNextBlock()
    {
        uint64_t addr = m_blocksToProcess.front();
        m_processingBlocks.erase(addr);
        m_blocksToProcess.pop();
        return addr;
    }

    // Returns true if the block at the given address has already been processed.
    bool HasSeenBlock(uint64_t addr) const
    {
        return m_processedBlocks.contains(addr);
    }

    // Marks a block as seen (discovered but not necessarily processed).
    void MarkBlockAsSeen(uint64_t addr)
    {
        m_processedBlocks.insert(addr);
    }

    // Marks a block as fully processed and removes it from the processing set.
    void MarkBlockAsProcessed(uint64_t addr)
    {
        m_processedBlocks.insert(addr);
        m_processingBlocks.erase(addr);
    }

    // Returns true if the block at the given address is currently being processed.
    bool IsBlockProcessing(uint64_t addr) const
    {
        return m_processingBlocks.contains(addr);
    }

    // Adds a new block to the context for tracking and processing.
    void AddBlock(uint64_t addr, BinaryNinja::Ref<BinaryNinja::BasicBlock> block)
    {
        m_blocks[addr] = block;
        m_currentBlock = block;
    }

    // Queues an address for future block processing.
    void QueueAddress(uint64_t addr)
    {
        if (m_processedBlocks.contains(addr) || m_processingBlocks.contains(addr))
            return;

        m_blocksToProcess.push(addr);
        m_processingBlocks.insert(addr);
    }

    // Returns the current basic block being analyzed.
    BinaryNinja::Ref<BinaryNinja::BasicBlock> GetCurrentBlock()
    {
        return m_currentBlock;
    }

    BinaryNinja::BasicBlockAnalysisContext& GetAnalysisContext()
    {
        return *m_ctx;
    }

    void AddCurrentInstructionData(const uint8_t* data, size_t len)
    {
        if (m_currentBlock && data && len > 0)
            m_currentBlock->AddInstructionData(data, len);
    }

    BinaryNinja::Function* GetFunction()
    {
        return m_function;
    }

    YSCFunctionContext* GetFunctionContext()
    {
        return m_functionContext.get();
    }

    void RecordEnter(const YSCEnterInfo& enter)
    {
        if (m_functionContext)
            m_functionContext->m_enter = enter;
    }

    void RecordReturnCount(uint8_t returnCount)
    {
        if (!m_functionContext)
            return;
        if (!m_functionContext->m_returnCount || returnCount > *m_functionContext->m_returnCount)
            m_functionContext->m_returnCount = returnCount;
    }

    void RecordSwitch(const YSCSwitchInfo& switchInfo)
    {
        if (m_functionContext)
            m_functionContext->m_switches[switchInfo.m_address] = switchInfo;
    }

    // Checks if the first instruction in the next block is an ENTER instruction.
    bool IsFirstInstructionEnter()
    {
        uint8_t insn;
        GetView()->Read(&insn, m_blocksToProcess.front(), 1);
        return insn == OP_ENTER;
    }

  private:
    BinaryNinja::Function* m_function; // The function being analyzed.
    std::queue<uint64_t> m_blocksToProcess; // Queue of block addresses to process.
    std::unordered_map<uint64_t, BinaryNinja::Ref<BinaryNinja::BasicBlock>> m_blocks; // Map of address to basic block objects.
    std::unordered_set<uint64_t> m_processedBlocks; // Set of addresses of blocks that have been processed.
    std::unordered_set<uint64_t> m_processingBlocks; // Set of addresses of blocks currently being processed.
    BinaryNinja::BasicBlockAnalysisContext* m_ctx; // The Binary Ninja block analysis context.
    std::unique_ptr<YSCFunctionContext> m_functionContext;
    bool m_shouldEndBlock = false; // Flag indicating if the current block should be ended.
    BinaryNinja::Ref<BinaryNinja::BasicBlock> m_currentBlock; // The current block being analyzed.
};

class YSCArchitecture : public BinaryNinja::ArchitectureWithFunctionContext<YSCFunctionContext>
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
        return YSC_MAX_INSTRUCTION_LENGTH;
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

    bool LiftFunction(BinaryNinja::LowLevelILFunction* function, BinaryNinja::FunctionLifterContext& context) override;

    void FreeFunctionArchContext(YSCFunctionContext* context) override;

  private:
    std::array<std::unique_ptr<OpBase>, OP_MAX> m_insns;
};

#endif

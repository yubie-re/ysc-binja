#ifndef YSC_BLOCK_ANALYSIS_CONTEXT_HPP
#define YSC_BLOCK_ANALYSIS_CONTEXT_HPP

#include "Architecture/FunctionContext.hpp"
#include "Instructions/OperationEnum.hpp"
#include <binaryninjaapi.h>
#include <cstdint>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>

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
    std::unordered_set<uint64_t> m_processingBlocks; // Set of addresses currently being processed.
    BinaryNinja::BasicBlockAnalysisContext* m_ctx; // The Binary Ninja block analysis context.
    std::unique_ptr<YSCFunctionContext> m_functionContext;
    bool m_shouldEndBlock = false; // Flag indicating if the current block should be ended.
    BinaryNinja::Ref<BinaryNinja::BasicBlock> m_currentBlock; // The current block being analyzed.
};

#endif

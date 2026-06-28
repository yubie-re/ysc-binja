#include "inc.hpp"
#include "Architecture/BlockAnalysisContext.hpp"

YSCBlockAnalysisContext::YSCBlockAnalysisContext(BinaryNinja::Function* function,
                                                 BinaryNinja::BasicBlockAnalysisContext* ctx)
    : m_function(function), m_ctx(ctx)
{
    m_functionContext = std::make_unique<YSCFunctionContext>();
    m_functionContext->m_start = function->GetStart();
    m_blocksToProcess.push(function->GetStart());
    m_processingBlocks.insert(function->GetStart());
}

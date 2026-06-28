#ifndef YSC_FUNCTION_FACTS_HPP
#define YSC_FUNCTION_FACTS_HPP

#include "Architecture/FunctionContext.hpp"
#include <binaryninjaapi.h>
#include <cstdint>
#include <optional>

uintptr_t GetYSCViewCacheKey(BinaryNinja::BinaryView* view);
bool IsYSCViewKeyRetired(uintptr_t key);
bool IsYSCArchitectureViewRetired(BinaryNinja::BinaryView* view);
bool IsEnterAt(BinaryNinja::BinaryView* view, uint64_t addr);
std::optional<YSCEnterInfo> ReadEnterInfo(BinaryNinja::BinaryView* view, uint64_t addr);
uint8_t GetEnterParamCount(BinaryNinja::BinaryView* view, uint64_t addr);
uint8_t FindFirstLeaveReturnCount(BinaryNinja::BinaryView* view, uint64_t addr);
void TraceYSCFunctionFactCounters(BinaryNinja::BinaryView* view);

#endif

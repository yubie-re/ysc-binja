#ifndef YSC_LIFTING_SUPPORT_HPP
#define YSC_LIFTING_SUPPORT_HPP

#include "Architecture/FunctionContext.hpp"
#include <binaryninjaapi.h>
#include <cstdint>
#include <optional>

std::optional<YSCSwitchInfo> DecodeYSCSwitchInfo(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* data,
                                                 size_t len);
bool IsYSCIndexedInstructionStart(BinaryNinja::BinaryView* view, uint64_t target, size_t maxInstructionLength);
bool IsYSCValidCallTarget(BinaryNinja::BinaryView* view, uint64_t target, size_t maxInstructionLength);

#endif

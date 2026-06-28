#ifndef YSC_INSTRUCTION_REGISTRY_HPP
#define YSC_INSTRUCTION_REGISTRY_HPP

#include "Instructions/OperationEnum.hpp"
#include <binaryninjaapi.h>
#include <cstddef>
#include <cstdint>

bool GetYSCInstructionInfo(uint8_t opcode, const uint8_t* data, uint64_t addr, size_t maxLen,
                           BinaryNinja::InstructionInfo& result);
void GetYSCInstructionText(uint8_t opcode, const uint8_t* data, uint64_t addr, size_t& len,
                           std::vector<BinaryNinja::InstructionTextToken>& result);
bool GetYSCInstructionLowLevelIL(uint8_t opcode, const uint8_t* data, uint64_t addr, size_t& len,
                                 BinaryNinja::LowLevelILFunction& il);

#endif

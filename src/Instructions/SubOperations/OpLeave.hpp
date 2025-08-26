#ifndef LEAVE_HPP
#define LEAVE_HPP

#include "../OperationBase.hpp"

class OpLeave : public OpBase
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
    bool GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result) override;
    bool GetInstructionBlockAnalysis(YSCBlockAnalysisContext& ctx, size_t address, size_t& bytesRead) override;
};

#endif

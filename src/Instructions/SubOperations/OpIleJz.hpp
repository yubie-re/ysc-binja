#ifndef ILEJZ_HPP
#define ILEJZ_HPP

#include "../OperationBase.hpp"

class OpIleJz : public OpBase
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
    bool GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result) override;
};

#endif

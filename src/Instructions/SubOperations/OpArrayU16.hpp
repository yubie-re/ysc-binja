#ifndef ARRAYU16_HPP
#define ARRAYU16_HPP

#include "../OperationBase.hpp"

class OpArrayU16 : public OpBase
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
};

#endif

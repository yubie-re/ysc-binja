#ifndef LOADN_HPP
#define LOADN_HPP

#include "../OperationBase.hpp"

class OpLoadN : public OpBase
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
    static bool GetInstructionLowLevelILAlternate(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il, int count);
};

#endif

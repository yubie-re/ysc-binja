#ifndef FEQ_HPP
#define FEQ_HPP

#include "../OperationBase.hpp"

class OpFeq : public OpBase
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
};

#endif

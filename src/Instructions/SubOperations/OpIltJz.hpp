#ifndef ILTJZ_HPP
#define ILTJZ_HPP

#include "../OperationBase.hpp"
#include "OpJz.hpp"

class OpIltJz : public OpJz
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
};

#endif

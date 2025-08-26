#ifndef IGEJZ_HPP
#define IGEJZ_HPP

#include "../OperationBase.hpp"
#include "OpJz.hpp"

class OpIgeJz : public OpJz
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
};

#endif

#ifndef ILEJZ_HPP
#define ILEJZ_HPP

#include "../OperationBase.hpp"
#include "OpJz.hpp"

class OpIleJz : public OpJz
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
};

#endif

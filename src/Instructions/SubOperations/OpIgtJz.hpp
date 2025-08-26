#ifndef IGTJZ_HPP
#define IGTJZ_HPP

#include "../OperationBase.hpp"
#include "OpJz.hpp"

class OpIgtJz : public OpJz
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
};

#endif

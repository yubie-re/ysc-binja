#ifndef OPERATION_BASE_HPP
#define OPERATION_BASE_HPP

#include "inc.hpp"
#include "OperandTypes.hpp"
#include <stdexcept>

class OpBase
{
public:
virtual size_t GetSize() { return 1; }
virtual std::string_view GetName()  {return "UNK"; }
virtual void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result);
virtual bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il);
virtual bool GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result);
template <typename T>
const T GetOperand(const uint8_t* data, size_t len, size_t offset)
{
    if(len >= sizeof(T) + offset)
        return *reinterpret_cast<const T*>(data + offset);
    else
        throw std::runtime_error("Operand overflow");
}
virtual bool CustomLLILSize() { return false; }

private:
};

#endif
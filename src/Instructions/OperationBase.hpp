#ifndef OPERATION_BASE_HPP
#define OPERATION_BASE_HPP

#include "inc.hpp"
#include "OperandTypes.hpp"
#include <stdexcept>

class YSCBlockAnalysisContext;

inline void AppendYSCGlobalOperandText(uint32_t operand, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    uint32_t block = operand >> 18;
    uint32_t offset = operand & 0x3ffff;
    result.push_back(BinaryNinja::InstructionTextToken(
        BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
    result.push_back(BinaryNinja::InstructionTextToken(
        BNInstructionTextTokenType::TextToken,
        fmt::format(" <Global_{} block={} offset={:#x}>", operand, block, offset)));
}

class OpBase
{
  public:
    virtual size_t GetSize()
    {
        return 1;
    }
    virtual std::string_view GetName()
    {
        return "UNK";
    }
    // Retrieves the disassembly text for the instruction at the given address.
    virtual void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len,
                                    std::vector<BinaryNinja::InstructionTextToken>& result);

    // Generates the Low Level IL for the instruction at the given address.
    virtual bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len,
                                          BinaryNinja::LowLevelILFunction& il);

    // Retrieves information about the instruction, such as branching or length.
    virtual bool GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen,
                                    BinaryNinja::InstructionInfo& result);

    // Performs custom block analysis for the instruction at the given address.
    virtual bool GetInstructionBlockAnalysis(YSCBlockAnalysisContext& ctx, size_t address, size_t& bytesRead);
    template <typename T> const T GetOperand(const uint8_t* data, size_t len, size_t offset)
    {
        if (len >= sizeof(T) + offset)
            return *reinterpret_cast<const T*>(data + offset);
        else
            throw std::runtime_error("Operand overflow");
    }

    template <typename T>
    T GetOperand(std::span<const uint8_t> data, size_t offset)
    {
        if (data.size() >= offset + sizeof(T))
            return *reinterpret_cast<const T*>(data.data() + offset);
        else
            throw std::runtime_error("Operand overflow");
    }

    virtual bool CustomLLILSize()
    {
        return false;
    }

  private:
};

#endif

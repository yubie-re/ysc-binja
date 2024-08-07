#ifndef PUSHCONSTFF_HPP
#define PUSHCONSTFF_HPP

#include "../OperationBase.hpp"
#include "inc.hpp"

class OpPushConstFloat : public OpBase
{
public:
    OpPushConstFloat(float f) : m_float(f) { };

    size_t GetSize() override { return 1; };

    std::string_view GetName() override
    {
        return "PUSH_CONST_FLOAT";
    }

    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override
    {
        OpBase::GetInstructionText(data, addr, len, result);
        result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{}", m_float)));
    }

    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override
    {
        il.AddInstruction(il.Push(4, il.FloatConstSingle(m_float)));
        return true;
    };
private:
    float m_float = 0.0;
};

#endif

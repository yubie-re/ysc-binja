#ifndef PUSHCONST_HPP
#define PUSHCONST_HPP

#include "../OperationBase.hpp"
#include "inc.hpp"

class OpPushConst : public OpBase
{
public:
    OpPushConst(int i) : m_int(i) { };

    size_t GetSize() override { return 1; };

    std::string_view GetName() override
    {
        return "PUSH_CONST";
    }

    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override
    {
        OpBase::GetInstructionText(data, addr, len, result);
        result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{}", m_int)));
    }

    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override
    {
        il.AddInstruction(il.Push(4, il.Const(4, m_int)));
        return true;
    };
private:
    int m_int = 0.0;
};

#endif

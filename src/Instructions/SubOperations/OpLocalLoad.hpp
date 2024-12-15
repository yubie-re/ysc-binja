#ifndef OP_LOCAL_LOAD_HPP
#define OP_LOCAL_LOAD_HPP

#include "../OperationBase.hpp"

template <typename T>
class OpLocalLoad : public OpBase
{
public:
    size_t GetSize() override
    {
        return 1 + sizeof(T);
    };

    std::string_view GetName() override
    {
        if constexpr (std::is_same_v<T, OpU16>)
        {
            return "LOCAL_U16_LOAD";
        }
        else if constexpr (std::is_same_v<T, OpU8>)
        {
            return "LOCAL_U8_LOAD";
        }
    }

    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override
    {
        if(len < sizeof(T))
            return;
        const T operand = GetOperand<T>(data, len, 0);
        OpBase::GetInstructionText(data, addr, len, result);
        result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand.ToValue()), operand.ToValue()));
    }

    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override
    {
        if(len < sizeof(T))
            return false;
        const auto operand = GetOperand<T>(data, len, 0).ToValue();
        il.AddInstruction(il.Push(4, il.Load(4, il.Sub(4, il.Register(4, Reg_FP), il.Const(4,operand * 4)))));
        return true;
    }
};

#endif

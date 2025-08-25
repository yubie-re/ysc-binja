#ifndef OP_PUSH_CONST_T_HPP
#define OP_PUSH_CONST_T_HPP

#include "../OperationBase.hpp"

template <typename T>
class OpPushConstT : public OpBase
{
public:
    size_t GetSize() override
    {
        return 1 + sizeof(T);
    };

    std::string_view GetName() override
    {
        if constexpr (std::is_same_v<T, OpU8>)
        {
            return "PUSH_CONST_U8";
        }
        else if constexpr (std::is_same_v<T, OpS16>)
        {
            return "PUSH_CONST_S16";
        }
        else if constexpr (std::is_same_v<T, OpU24>)
        {
            return "PUSH_CONST_U24";
        }
        else if constexpr (std::is_same_v<T, OpU32>)
        {
            return "PUSH_CONST_U32";
        }
        else if constexpr (std::is_same_v<T, OpF>)
        {
            return "PUSH_CONST_F";
        }
    }

    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override
    {
        if(len < sizeof(T))
            return;
        const T operand = GetOperand<T>(data, len, 0);
        OpBase::GetInstructionText(data, addr, len, result);
        if constexpr (std::is_same_v<T, OpF>)
        {
            result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:.1f}", operand.ToValue()), operand.ToValue()));
        }
        else
            result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand.ToValue()), operand.ToValue()));
    }

    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override
    {
        if(len < sizeof(T))
            return false;
        const auto operand = GetOperand<T>(data, len, 0).ToValue();
        if constexpr (std::is_same_v<T, OpF>)
        {
            il.AddInstruction(il.Push(4, il.FloatConstSingle(operand)));
        }
        else
        {
            il.AddInstruction(il.Push(4, il.Const(4, operand)));
        }
        return true;
    }
};

#endif

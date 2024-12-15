#ifndef IOFFSET_T_HPP
#define IOFFSET_T_HPP

#include "../OperationBase.hpp"

template <typename T>
class OpIoffsetT : public OpBase
{
public:
    size_t GetSize() override
    {
        return 1 + sizeof(T);
    };

    std::string_view GetName() override
    {
        if constexpr (std::is_same_v<T, OpS16>)
        {
            return "IOFFSET_S16";
        }
        else if constexpr (std::is_same_v<T, OpU8>)
        {
            return "IOFFSET_U8";
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
        il.AddInstruction(il.Push(4, il.Add(4, il.Pop(4), il.Const(4, static_cast<int>(operand) * 4))));
        return true;
    }
};

#endif

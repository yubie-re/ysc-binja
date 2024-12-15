#ifndef ARRAYSTORE_HPP
#define ARRAYSTORE_HPP

#include "../OperationBase.hpp"

template <typename T>
class OpArrayStore : public OpBase
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
            return "ARRAY_U8_STORE";
        }
        else if constexpr (std::is_same_v<T, OpU16>)
        {
            return "ARRAY_U16_STORE";
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
        il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4))); // Address
        il.AddInstruction(il.SetRegister(4, Reg_R2, il.Pop(4))); // Offset
        il.AddInstruction(il.SetRegister(4, Reg_R3, il.Pop(4))); // Value
        auto one_plus_idx_times_imm = il.Add(4, il.Const(4, 1), il.Mult(4, il.Const(4, operand), il.Register(4, Reg_R2)));
        auto expr = il.Store(4, il.Add(4, il.Register(4, Reg_R1), one_plus_idx_times_imm), il.Register(4, Reg_R3));
        il.AddInstruction(expr);
        return true;
    }
};

#endif

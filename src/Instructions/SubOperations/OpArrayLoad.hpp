#ifndef ARRAYLOAD_HPP
#define ARRAYLOAD_HPP

#include "../OperationBase.hpp"

template <typename T>
class OpArrayLoad : public OpBase
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
            return "ARRAY_U8_LOAD";
        }
        else if constexpr (std::is_same_v<T, OpU16>)
        {
            return "ARRAY_U16_LOAD";
        }
    }

    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override
    {
        if(len < sizeof(T))
            return;
        const auto operand = GetOperand<T>(data, len, 0).ToValue();
        OpBase::GetInstructionText(data, addr, len, result);
        result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
    }

    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override
    {
        if(len < sizeof(T))
            return false;
        const auto operand = GetOperand<T>(data, len, 0).ToValue();
        il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4))); // Address
        il.AddInstruction(il.SetRegister(4, Reg_R2, il.Pop(4))); // Offset
        auto one_plus_idx_times_imm = il.Add(4, il.Const(4, 1), il.Mult(4, il.Const(4, operand), il.Register(4, Reg_R2)));
        auto expr = il.Push(4, il.Load(4, il.Add(4, il.Register(4, Reg_R1), one_plus_idx_times_imm)));
        il.AddInstruction(expr);
        return true;
    }
};

#endif

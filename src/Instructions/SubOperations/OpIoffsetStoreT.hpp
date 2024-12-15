#ifndef IOFFSET_STORE_T_HPP
#define IOFFSET_STORE_T_HPP

#include "../OperationBase.hpp"

template <typename T>
class OpIoffsetStoreT : public OpBase
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
            return "IOFFSET_S16_STORE";
        }
        else if constexpr (std::is_same_v<T, OpU8>)
        {
            return "IOFFSET_U8_STORE";
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
        il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4)));
        il.AddInstruction(
            il.Store(4, 
                il.Add(4, 
                    il.Register(4, Reg_R1), 
                    il.Const(4, static_cast<int>(operand) * 8)),
            il.Pop(4)));
        return true;
    }
};

#endif

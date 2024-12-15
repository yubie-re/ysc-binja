#ifndef STATICU8LOAD_HPP
#define STATICU8LOAD_HPP

#include "../OperationBase.hpp"
#include "../OperandTypes.hpp"

template <typename T>
class OpStaticLoad : public OpBase
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
            return "STATIC_U8_LOAD";
        }
        else if constexpr (std::is_same_v<T, OpU16>)
        {
            return "STATIC_U16_LOAD";
        }
        else if constexpr (std::is_same_v<T, OpU24>)
        {
            return "STATIC_U24_LOAD";
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
        if(!il.GetFunction())
            return false;
        if(!il.GetFunction()->GetView())
            return false;
        if(!il.GetFunction()->GetView()->GetSectionByName("STATICS"))
            return false;
        const auto operand = GetOperand<T>(data, len, 0).ToValue();
        il.AddInstruction(il.Push(4, il.Load(4, il.Const(4, 4 * operand + il.GetFunction()->GetView()->GetSectionByName("STATICS")->GetStart()))));
        return true;
    }
};


#endif

#ifndef STATICSTORE_HPP
#define STATICSTORE_HPP

#include "../OperationBase.hpp"
#include "../OperandTypes.hpp"

template <typename T>
class OpStaticStore : public OpBase
{
public:
    size_t GetSize() override
    {
        return 1 + sizeof(T);
    };

    std::string_view GetName() override
    {
        if constexpr (std::is_same_v<T, Op8>)
        {
            return "STATIC_U8_STORE";
        }
        else if constexpr (std::is_same_v<T, Op16>)
        {
            return "STATIC_U16_STORE";
        }
        else if constexpr (std::is_same_v<T, Op24>)
        {
            return "STATIC_U24_STORE";
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
        il.AddInstruction(il.Store(4, il.ConstPointer(4, 8 * operand + il.GetFunction()->GetView()->GetSectionByName("STATICS")->GetStart()), il.Pop(4)));
        return true;
    }
};

#endif

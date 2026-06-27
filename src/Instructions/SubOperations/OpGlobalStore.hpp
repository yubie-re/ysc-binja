#ifndef GLOBAL_STOREHPP
#define GLOBAL_STOREHPP

#include "../OperationBase.hpp"

namespace
{
inline BinaryNinja::Ref<BinaryNinja::Type> YSCVolatileGlobalStoreInt32Type()
{
    BinaryNinja::TypeBuilder builder(BinaryNinja::Type::IntegerType(4, true));
    builder.SetVolatile(BinaryNinja::Confidence<bool>(true));
    return builder.Finalize();
}
}

template <typename T>
class OpGlobalStore : public OpBase
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
            return "GLOBAL_U16_STORE";
        }
        else if constexpr (std::is_same_v<T, OpU24>)
        {
            return "GLOBAL_U24_STORE";
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
        const uint32_t operand = static_cast<const uint32_t>(GetOperand<T>(data, len, 0).ToValue());
        if(!il.GetFunction())
            return false;
        if(!il.GetFunction()->GetView())
            return false;
        if(!il.GetFunction()->GetView()->GetSectionByName("GLOBALS"))
            return false;
        uint32_t block = operand >> 18;
        uint32_t blockSize = 1 << 18;
        uint32_t needle = operand & 0x3FFFF;
        auto view = il.GetFunction()->GetView();
        uint32_t virtualAddress = view->GetSectionByName("GLOBALS")->GetStart() + (blockSize * block + needle) * 4;
        il.AddInstruction(il.Store(4, il.ConstPointer(4, virtualAddress), il.Pop(4)));
        return true;
    }
};

#endif

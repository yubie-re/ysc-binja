#ifndef PUSHCONST_HPP
#define PUSHCONST_HPP

#include "../OperationBase.hpp"
#include "inc.hpp"
#include "../OperationEnum.hpp"
#include "OpStoreN.hpp"
#include "OpLoadN.hpp"

class OpPushConst : public OpBase
{
public:
    OpPushConst(int i) : m_int(i) { };

    size_t GetSize() override { return 1; };

    virtual bool CustomLLILSize() override { return true; }

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
        auto arch = BinaryNinja::Architecture::GetByName("YSC");
        // For the case where it's not a STORE/lOAD_N in the future
        auto genericOut = [&]() 
        {
            il.AddInstruction(il.Push(4, il.Const(4, m_int)));
            len = GetSize();
            return true;
        };
        BinaryNinja::LowLevelILFunction tempIlFunction(BinaryNinja::Architecture::GetByName("YSC"), il.GetFunction()); // temp IL function to figure the insn size
        size_t insnLen2 = len - GetSize(); // How much of the buffer is left after the first insn
        bool ilSuccess = arch->GetInstructionLowLevelIL(data + GetSize() - 1, addr + GetSize(), insnLen2, tempIlFunction);
        if(!ilSuccess || len <= (GetSize() + insnLen2 + 1) || insnLen2 == 0)
            return genericOut();
        auto thirdInsn = data[GetSize() + insnLen2 - 1];
        if(thirdInsn == OP_STORE_N || thirdInsn == OP_LOAD_N)
        {
            insnLen2 = len - GetSize();
            il.SetCurrentAddress(arch, addr + GetSize());
            if(!arch->GetInstructionLowLevelIL(data + GetSize() - 1, addr + GetSize(), insnLen2, il))
                return genericOut();
            il.SetCurrentAddress(arch, addr + GetSize() + insnLen2);
            size_t len3 = len - GetSize() - insnLen2;
            if(thirdInsn == OP_STORE_N)
                OpStoreN::GetInstructionLowLevelILAlternate(data + GetSize() - 1 + insnLen2, addr + GetSize() + insnLen2, len3, il, m_int);
            else
                OpLoadN::GetInstructionLowLevelILAlternate(data + GetSize() - 1 + insnLen2, addr + GetSize() + insnLen2, len3, il, m_int);
            len = insnLen2 + GetSize() + len3;
        }
        else
            return genericOut();
        return true;
    };
private:
    int m_int = 0.0;
};

#endif

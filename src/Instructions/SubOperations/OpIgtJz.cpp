#include "inc.hpp"
#include "OpIgtJz.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpIgtJz::GetSize()
{
    return 3;
}

std::string_view OpIgtJz::GetName()
{
    return "IGT_JZ";
}

bool OpIgtJz::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int32_t operand = static_cast<int32_t>(addr) + static_cast<int32_t>(*reinterpret_cast<const int16_t*>(data)) + 3;
    auto t = BinaryNinja::LowLevelILLabel();
    auto f = BinaryNinja::LowLevelILLabel();
    il.AddInstruction(il.SetRegister(4, Reg_R2, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4)));
    il.AddInstruction(il.If(il.CompareSignedGreaterThan(4, il.Register(4, Reg_R1), il.Register(4, Reg_R2)), t, f));
    il.MarkLabel(f);
    auto branchIlLabelPtr = il.GetLabelForAddress(BinaryNinja::Architecture::GetByName("YSC"), operand);
    if(branchIlLabelPtr)
        il.AddInstruction(il.Goto(*branchIlLabelPtr));
    else
        il.AddInstruction(il.Jump(il.ConstPointer(4, operand)));
    il.MarkLabel(t);
    return true;
}

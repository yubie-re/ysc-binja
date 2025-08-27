#include "inc.hpp"
#include "OpIltJz.hpp"

size_t OpIltJz::GetSize()
{
    return 3;
}

std::string_view OpIltJz::GetName()
{
    return "ILT_JZ";
}

bool OpIltJz::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int32_t operand = static_cast<int32_t>(addr) + static_cast<int32_t>(*reinterpret_cast<const int16_t*>(data)) + 3;
    auto t = BinaryNinja::LowLevelILLabel();
    auto f = BinaryNinja::LowLevelILLabel();
    il.AddInstruction(il.If(il.CompareSignedLessThan(4, il.Pop(4), il.Pop(4)), t, f));
    il.MarkLabel(f);
    auto branchIlLabelPtr = il.GetLabelForAddress(BinaryNinja::Architecture::GetByName("YSC"), operand);
    if(branchIlLabelPtr)
        il.AddInstruction(il.Goto(*branchIlLabelPtr));
    else
        il.AddInstruction(il.Jump(il.Const(4, operand)));
    il.MarkLabel(t);
    return true;
}
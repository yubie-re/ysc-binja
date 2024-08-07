#include "inc.hpp"
#include "OpIsBitSet.hpp"

size_t OpIsBitSet::GetSize()
{
    return 1;
}

std::string_view OpIsBitSet::GetName()
{
    return "IS_BIT_SET";
}

bool OpIsBitSet::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.CompareNotEqual(4, il.Const(4, 0), il.And(4, il.Pop(4), il.ShiftLeft(4, il.Const(4, 1), il.Pop(4))))));
    return true;
}

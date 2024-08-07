#include "inc.hpp"
#include "OpFne.hpp"

size_t OpFne::GetSize()
{
    return 1;
}

std::string_view OpFne::GetName()
{
    return "FNE";
}

bool OpFne::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatCompareNotEqual(4, il.Pop(4), il.Pop(4))));
    return true;
}

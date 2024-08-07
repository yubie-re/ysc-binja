#include "inc.hpp"
#include "OpIne.hpp"

size_t OpIne::GetSize()
{
    return 1;
}

std::string_view OpIne::GetName()
{
    return "INE";
}

bool OpIne::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.CompareNotEqual(4, il.Pop(4), il.Pop(4))));
    return true;
}

#include "inc.hpp"
#include "OpIadd.hpp"

size_t OpIadd::GetSize()
{
    return 1;
}

std::string_view OpIadd::GetName()
{
    return "IADD";
}

bool OpIadd::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Add(4, il.Pop(4), il.Pop(4))));
    return true;
}

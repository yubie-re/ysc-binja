#include "inc.hpp"
#include "OpIlt.hpp"

size_t OpIlt::GetSize()
{
    return 1;
}

std::string_view OpIlt::GetName()
{
    return "ILT";
}

bool OpIlt::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.CompareSignedLessThan(4, il.Pop(4), il.Pop(4))));
    return true;
}

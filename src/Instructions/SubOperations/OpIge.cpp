#include "inc.hpp"
#include "OpIge.hpp"

size_t OpIge::GetSize()
{
    return 1;
}

std::string_view OpIge::GetName()
{
    return "IGE";
}

bool OpIge::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.CompareSignedGreaterEqual(4, il.Pop(4), il.Pop(4))));
    return true;
}

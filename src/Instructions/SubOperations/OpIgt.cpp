#include "inc.hpp"
#include "OpIgt.hpp"

size_t OpIgt::GetSize()
{
    return 1;
}

std::string_view OpIgt::GetName()
{
    return "IGT";
}

bool OpIgt::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.CompareSignedGreaterThan(4, il.Pop(4), il.Pop(4))));
    return true;
}

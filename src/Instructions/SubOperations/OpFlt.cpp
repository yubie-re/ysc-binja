#include "inc.hpp"
#include "OpFlt.hpp"

size_t OpFlt::GetSize()
{
    return 1;
}

std::string_view OpFlt::GetName()
{
    return "FLT";
}

bool OpFlt::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatCompareLessThan(4, il.Pop(4), il.Pop(4))));
    return true;
}

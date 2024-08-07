#include "inc.hpp"
#include "OpFge.hpp"

size_t OpFge::GetSize()
{
    return 1;
}

std::string_view OpFge::GetName()
{
    return "FGE";
}

bool OpFge::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatCompareGreaterEqual(4, il.Pop(4), il.Pop(4))));
    return true;
}

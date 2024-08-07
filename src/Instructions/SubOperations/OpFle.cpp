#include "inc.hpp"
#include "OpFle.hpp"

size_t OpFle::GetSize()
{
    return 1;
}

std::string_view OpFle::GetName()
{
    return "FLE";
}

bool OpFle::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatCompareLessEqual(4, il.Pop(4), il.Pop(4))));
    return true;
}

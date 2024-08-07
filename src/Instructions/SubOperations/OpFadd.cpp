#include "inc.hpp"
#include "OpFadd.hpp"

size_t OpFadd::GetSize()
{
    return 1;
}

std::string_view OpFadd::GetName()
{
    return "FADD";
}

bool OpFadd::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatAdd(4, il.Pop(4), il.Pop(4))));
    return true;
}

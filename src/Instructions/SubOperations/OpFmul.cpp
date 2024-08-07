#include "inc.hpp"
#include "OpFmul.hpp"

size_t OpFmul::GetSize()
{
    return 1;
}

std::string_view OpFmul::GetName()
{
    return "FMUL";
}

bool OpFmul::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatMult(4, il.Pop(4), il.Pop(4))));
    return true;
}

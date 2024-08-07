#include "inc.hpp"
#include "OpFneg.hpp"

size_t OpFneg::GetSize()
{
    return 1;
}

std::string_view OpFneg::GetName()
{
    return "FNEG";
}

bool OpFneg::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatNeg(4, il.Pop(4))));
    return true;
}

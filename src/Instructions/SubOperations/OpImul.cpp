#include "inc.hpp"
#include "OpImul.hpp"

size_t OpImul::GetSize()
{
    return 1;
}

std::string_view OpImul::GetName()
{
    return "IMUL";
}

bool OpImul::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Mult(4, il.Pop(4), il.Pop(4))));
    return true;
}

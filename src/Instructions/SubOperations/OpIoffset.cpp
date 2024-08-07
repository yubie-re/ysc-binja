#include "inc.hpp"
#include "OpIoffset.hpp"

size_t OpIoffset::GetSize()
{
    return 1;
}

std::string_view OpIoffset::GetName()
{
    return "IOFFSET";
}

bool OpIoffset::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.ConstPointer(4, il.Add(4, il.Pop(4), il.Mult(4, il.Pop(4), il.Const(4, 8))))));
    return true;
}

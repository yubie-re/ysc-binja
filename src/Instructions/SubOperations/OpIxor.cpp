#include "inc.hpp"
#include "OpIxor.hpp"

size_t OpIxor::GetSize()
{
    return 1;
}

std::string_view OpIxor::GetName()
{
    return "IXOR";
}

bool OpIxor::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Xor(4, il.Pop(4), il.Pop(4))));
    return true;
}

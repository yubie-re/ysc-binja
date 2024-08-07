#include "inc.hpp"
#include "OpImod.hpp"

size_t OpImod::GetSize()
{
    return 1;
}

std::string_view OpImod::GetName()
{
    return "IMOD";
}

bool OpImod::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.ModSigned(4, il.Pop(4), il.Pop(4))));
    return true;
}

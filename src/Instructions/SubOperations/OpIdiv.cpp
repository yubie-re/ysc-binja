#include "inc.hpp"
#include "OpIdiv.hpp"

size_t OpIdiv::GetSize()
{
    return 1;
}

std::string_view OpIdiv::GetName()
{
    return "IDIV";
}

bool OpIdiv::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.DivSigned(4, il.Pop(4), il.Pop(4))));
    return true;
}

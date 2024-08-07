#include "inc.hpp"
#include "OpIand.hpp"

size_t OpIand::GetSize()
{
    return 1;
}

std::string_view OpIand::GetName()
{
    return "IAND";
}

bool OpIand::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.And(4, il.Pop(4), il.Pop(4))));
    return true;
}

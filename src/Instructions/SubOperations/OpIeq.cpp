#include "inc.hpp"
#include "OpIeq.hpp"

size_t OpIeq::GetSize()
{
    return 1;
}

std::string_view OpIeq::GetName()
{
    return "IEQ";
}

bool OpIeq::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.CompareEqual(4, il.Pop(4), il.Pop(4))));
    return true;
}

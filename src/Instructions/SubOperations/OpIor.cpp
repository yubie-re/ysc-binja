#include "inc.hpp"
#include "OpIor.hpp"

size_t OpIor::GetSize()
{
    return 1;
}

std::string_view OpIor::GetName()
{
    return "IOR";
}

bool OpIor::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Or(4, il.Pop(4), il.Pop(4))));
    return true;
}

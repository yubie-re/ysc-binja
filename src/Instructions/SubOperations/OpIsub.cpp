#include "inc.hpp"
#include "OpIsub.hpp"

size_t OpIsub::GetSize()
{
    return 1;
}

std::string_view OpIsub::GetName()
{
    return "ISUB";
}

bool OpIsub::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Sub(4, il.Pop(4), il.Pop(4))));
    return true;
}

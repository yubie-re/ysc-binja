#include "inc.hpp"
#include "OpDrop.hpp"

size_t OpDrop::GetSize()
{
    return 1;
}

std::string_view OpDrop::GetName()
{
    return "DROP";
}

bool OpDrop::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Pop(4));
    return true;
}

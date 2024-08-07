#include "inc.hpp"
#include "OpCatch.hpp"

size_t OpCatch::GetSize()
{
    return 1;
}

std::string_view OpCatch::GetName()
{
    return "CATCH";
}

bool OpCatch::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Const(4, -1)));
    return true;
}

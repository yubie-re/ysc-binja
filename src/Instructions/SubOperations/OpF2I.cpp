#include "inc.hpp"
#include "OpF2I.hpp"

size_t OpF2I::GetSize()
{
    return 1;
}

std::string_view OpF2I::GetName()
{
    return "F2I";
}

bool OpF2I::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatToInt(4, il.Pop(4))));  
    return true;
}

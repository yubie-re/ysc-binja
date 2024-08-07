#include "inc.hpp"
#include "OpLoad.hpp"

size_t OpLoad::GetSize()
{
    
    return 1;
}

std::string_view OpLoad::GetName()
{
    return "LOAD";
}

bool OpLoad::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Load(4, il.Pop(4))));
    return true;
}

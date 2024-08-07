#include "inc.hpp"
#include "OpIneg.hpp"

size_t OpIneg::GetSize()
{
    return 1;
}

std::string_view OpIneg::GetName()
{
    return "INEG";
}

bool OpIneg::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Neg(4, il.Pop(4))));
    return true;
}

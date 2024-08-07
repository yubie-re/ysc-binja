#include "inc.hpp"
#include "OpI2F.hpp"

size_t OpI2F::GetSize()
{
    return 1;
}

std::string_view OpI2F::GetName()
{
    return "I2F";
}

bool OpI2F::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.IntToFloat(4, il.Pop(4))));
    return true;
}

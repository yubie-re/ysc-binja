#include "inc.hpp"
#include "OpLoadN.hpp"

size_t OpLoadN::GetSize()
{
    return 1;
}

std::string_view OpLoadN::GetName()
{
    return "LOAD_N";
}

bool OpLoadN::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SystemCall());
    return true;
}
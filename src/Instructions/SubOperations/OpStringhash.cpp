#include "inc.hpp"
#include "OpStringhash.hpp"

size_t OpStringhash::GetSize()
{
    return 1;
}

std::string_view OpStringhash::GetName()
{
    return "STRINGHASH";
}

bool OpStringhash::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SystemCall());
    return true;
}

#include "inc.hpp"
#include "OpStoreN.hpp"

size_t OpStoreN::GetSize()
{
    return 1;
}

std::string_view OpStoreN::GetName()
{
    return "STORE_N";
}

bool OpStoreN::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SystemCall());
    return true;
}

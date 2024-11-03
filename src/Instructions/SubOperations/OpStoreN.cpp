#include "inc.hpp"
#include "OpStoreN.hpp"
#include "Architecture/YSCArchitecture.hpp"
#include "lowlevelilinstruction.h"

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
    return il.AddInstruction(il.Unimplemented());
}

#include "inc.hpp"
#include "OpStoreRev.hpp"

size_t OpStoreRev::GetSize()
{
    return 1;
}

std::string_view OpStoreRev::GetName()
{
    return "STORE_REV";
}

bool OpStoreRev::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Store(4, il.Pop(4), il.Pop(4)));
    //TODO: THIS IS WRONG.
    return true;
}

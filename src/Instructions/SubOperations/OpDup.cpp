#include "inc.hpp"
#include "OpDup.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpDup::GetSize()
{
    return 1;
}

std::string_view OpDup::GetName()
{
    return "DUP";
}

bool OpDup::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Load(4, il.Register(4, Reg_SP))));
    return true;
}

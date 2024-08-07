#include "inc.hpp"
#include "OpF2V.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpF2V::GetSize()
{
    return 1;
}

std::string_view OpF2V::GetName()
{
    return "F2V";
}

bool OpF2V::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.Load(4, il.Register(4, Reg_SP))));
    il.AddInstruction(il.Push(4, il.Load(4, il.Register(4, Reg_SP))));
    return true;
}

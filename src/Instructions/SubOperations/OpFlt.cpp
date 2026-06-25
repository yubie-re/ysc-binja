#include "inc.hpp"
#include "OpFlt.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpFlt::GetSize()
{
    return 1;
}

std::string_view OpFlt::GetName()
{
    return "FLT";
}

bool OpFlt::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SetRegister(4, Reg_R2, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4)));
    il.AddInstruction(il.Push(4, il.FloatCompareLessThan(4, il.Register(4, Reg_R1), il.Register(4, Reg_R2))));
    return true;
}

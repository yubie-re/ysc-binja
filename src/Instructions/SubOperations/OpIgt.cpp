#include "inc.hpp"
#include "OpIgt.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpIgt::GetSize()
{
    return 1;
}

std::string_view OpIgt::GetName()
{
    return "IGT";
}

bool OpIgt::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SetRegister(4, Reg_R2, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4)));
    il.AddInstruction(il.Push(4, il.CompareSignedGreaterThan(4, il.Register(4, Reg_R1), il.Register(4, Reg_R2))));
    return true;
}

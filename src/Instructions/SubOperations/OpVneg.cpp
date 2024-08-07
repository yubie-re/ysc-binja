#include "inc.hpp"
#include "OpVneg.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpVneg::GetSize()
{
    return 1;
}

std::string_view OpVneg::GetName()
{
    return "VNEG";
}

bool OpVneg::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SetRegister(4, Reg_VZ1, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VY1, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VX1, il.Pop(4)));

    il.AddInstruction(il.Push(4, il.FloatNeg(4, il.Register(4, Reg_VX1))));
    il.AddInstruction(il.Push(4, il.FloatNeg(4, il.Register(4, Reg_VY1))));
    il.AddInstruction(il.Push(4, il.FloatNeg(4, il.Register(4, Reg_VZ1))));

    return true;
}

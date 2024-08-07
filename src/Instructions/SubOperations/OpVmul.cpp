#include "inc.hpp"
#include "OpVmul.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpVmul::GetSize()
{
    return 1;
}

std::string_view OpVmul::GetName()
{
    return "VMUL";
}

bool OpVmul::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SetRegister(4, Reg_VZ1, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VY1, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VX1, il.Pop(4)));

    il.AddInstruction(il.SetRegister(4, Reg_VZ2, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VY2, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VX2, il.Pop(4)));

    il.AddInstruction(il.Push(4, il.FloatMult(4, il.Register(4, Reg_VX2), il.Register(4, Reg_VX1))));
    il.AddInstruction(il.Push(4, il.FloatMult(4, il.Register(4, Reg_VY2), il.Register(4, Reg_VY1))));
    il.AddInstruction(il.Push(4, il.FloatMult(4, il.Register(4, Reg_VZ2), il.Register(4, Reg_VZ1))));

    return true;
}

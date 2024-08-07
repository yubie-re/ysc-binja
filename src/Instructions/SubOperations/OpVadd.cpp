#include "inc.hpp"
#include "OpVadd.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpVadd::GetSize()
{
    return 1;
}

std::string_view OpVadd::GetName()
{
    return "VADD";
}


bool OpVadd::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SetRegister(4, Reg_VZ1, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VY1, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VX1, il.Pop(4)));

    il.AddInstruction(il.SetRegister(4, Reg_VZ2, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VY2, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_VX2, il.Pop(4)));

    il.AddInstruction(il.Push(4, il.FloatAdd(4, il.Register(4, Reg_VX2), il.Register(4, Reg_VX1))));
    il.AddInstruction(il.Push(4, il.FloatAdd(4, il.Register(4, Reg_VY2), il.Register(4, Reg_VY1))));
    il.AddInstruction(il.Push(4, il.FloatAdd(4, il.Register(4, Reg_VZ2), il.Register(4, Reg_VZ1))));
    return true;
}

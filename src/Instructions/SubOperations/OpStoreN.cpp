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
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4))); // address
    il.AddInstruction(il.SetRegister(4, Reg_R2, il.Pop(4))); // Max count
    il.AddInstruction(il.SetRegister(4, Reg_R3, il.Const(4, 0))); // i
    il.AddInstruction(il.SetRegister(4, Reg_R4, il.Add(4, il.Register(4, Reg_R1), il.Mult(4, il.Const(4, 4), il.Register(4, Reg_R2))))); // T0
    auto loopStart = BinaryNinja::LowLevelILLabel();
    auto loopData = BinaryNinja::LowLevelILLabel();
    auto loopExit = BinaryNinja::LowLevelILLabel();
    il.MarkLabel(loopStart);
    auto isLoop = il.CompareSignedLessThan(4, il.Register(4, Reg_R3), il.Register(4, Reg_R2));
    il.AddInstruction(il.If(isLoop, loopData, loopExit)); // if i < count -> loop else exit
    il.MarkLabel(loopData);
    auto loadAddress = il.Sub(4, il.Register(4, Reg_R4), il.Mult(4, il.Const(4, 4), il.Register(4, Reg_R3)));
    il.AddInstruction(il.Store(4, loadAddress, il.Pop(4))); // [(4 * (count - i - 1)) + addr].d = pop
    il.AddInstruction(il.SetRegister(4, Reg_R3, il.Add(4, il.Register(4, Reg_R3), il.Const(4, 1)))); // i++
    il.AddInstruction(il.Goto(loopStart));
    il.MarkLabel(loopExit);
    return true;
}


bool OpStoreN::GetInstructionLowLevelILAlternate(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il, int unrolledCount) 
{
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4))); // address
    for(int i = 0; i < unrolledCount; i++)
    {
        auto loadAddress = il.Add(4, il.Register(4, Reg_R1), il.Const(4, 4 * (unrolledCount - i - 1)));
        il.AddInstruction(il.Store(4, loadAddress, il.Pop(4)));
    }
    len = 1;
    return true;
}
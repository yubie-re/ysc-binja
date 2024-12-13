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

/*
    auto t = BinaryNinja::LowLevelILLabel();
    auto f = BinaryNinja::LowLevelILLabel();
    il.AddInstruction(il.If(il.CompareSignedGreaterThan(4, il.Pop(4), il.Pop(4)), t, f));
    il.MarkLabel(f);
    auto branchIlLabelPtr = il.GetLabelForAddress(BinaryNinja::Architecture::GetByName("YSC"), operand);
    if(branchIlLabelPtr)
        il.AddInstruction(il.Goto(*branchIlLabelPtr));
    else
        il.AddInstruction(il.Jump(il.Const(4, operand)));
    il.MarkLabel(t);
    return true;
*/

bool OpStoreN::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_R2, il.Sub(4, il.Pop(4), il.Const(4, 1))));
    auto loopStart = BinaryNinja::LowLevelILLabel();
    auto loopData = BinaryNinja::LowLevelILLabel();
    auto loopExit = BinaryNinja::LowLevelILLabel();
    il.MarkLabel(loopStart);
    auto isExit = il.CompareSignedGreaterEqual(4, il.Register(4, Reg_R2), il.Const(4, 0));
    il.AddInstruction(il.If(isExit, loopExit, loopData));
    il.MarkLabel(loopData);
    auto loadOffset = il.Mult(4, il.Const(4, 4), il.Register(4, Reg_R2));
    auto loadAddress = il.Add(4, loadOffset, il.Register(4, Reg_R1));
    il.AddInstruction(il.Store(4, il.ConstPointer(4, loadAddress), il.Pop(4)));
    il.AddInstruction(il.SetRegister(4, Reg_R2, il.Sub(4, il.Register(4, Reg_R2), il.Const(4, 1))));
    il.AddInstruction(il.Goto(loopStart));
    il.MarkLabel(loopExit);
    return true;
}

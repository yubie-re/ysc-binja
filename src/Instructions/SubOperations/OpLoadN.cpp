#include "inc.hpp"
#include "OpLoadN.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpLoadN::GetSize()
{
    return 1;
}

std::string_view OpLoadN::GetName()
{
    return "LOAD_N";
}

bool OpLoadN::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4))); // address
    il.AddInstruction(il.SetRegister(4, Reg_R2, il.Pop(4))); // Max count
    il.AddInstruction(il.SetRegister(4, Reg_R3, il.Const(4, 0))); // i
    auto loopStart = BinaryNinja::LowLevelILLabel();
    auto loopData = BinaryNinja::LowLevelILLabel();
    auto loopExit = BinaryNinja::LowLevelILLabel();
    il.MarkLabel(loopStart);
    auto isLoop = il.CompareSignedLessThan(4, il.Register(4, Reg_R3), il.Register(4, Reg_R2));
    il.AddInstruction(il.If(isLoop, loopData, loopExit)); // if i < count -> loop else exit
    il.MarkLabel(loopData);
    auto loadOffset = il.Mult(4, il.Const(4, 4), il.Register(4, Reg_R3));
    auto loadAddress = il.Add(4, loadOffset, il.Register(4, Reg_R1));
    il.AddInstruction(il.Push(4, il.Load(4, loadAddress))); // push(*(loadAddress[i]))
    il.AddInstruction(il.SetRegister(4, Reg_R3, il.Add(4, il.Register(4, Reg_R3), il.Const(4, 1)))); // i++
    il.AddInstruction(il.Goto(loopStart));
    il.MarkLabel(loopExit);
    return true;
}

bool OpLoadN::GetInstructionLowLevelILAlternate(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il, int count)
{
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4))); // address
    for(int i = 0; i < count; i++)
    {
        auto loadAddress = il.Add(4, il.Register(4, Reg_R1), il.Const(4, 4 * i));
        il.AddInstruction(il.Push(4, il.Load(4, loadAddress)));
    }
    len = 1;
    return true;

}
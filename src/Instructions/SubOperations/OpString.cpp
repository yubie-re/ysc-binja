#include "inc.hpp"
#include "OpString.hpp"

size_t OpString::GetSize()
{
    return 1;
}

std::string_view OpString::GetName()
{
    return "STRING";
}

bool OpString::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    if(il.GetFunction() && il.GetFunction()->GetView())
    {
        if(auto section = il.GetFunction()->GetView()->GetSectionByName("STRINGS"))
        {
            il.AddInstruction(il.Push(4, il.Add(4, il.Const(4, section->GetStart()), il.Pop(4))));
        }
    }
    return true;
}

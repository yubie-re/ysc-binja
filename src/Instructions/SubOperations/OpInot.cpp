#include "inc.hpp"
#include "OpInot.hpp"

size_t OpInot::GetSize()
{
    return 1;
}

std::string_view OpInot::GetName()
{
    return "INOT";
}

bool OpInot::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.CompareNotEqual(4, il.Const(4, 0), il.Pop(4))));
    return true;
}
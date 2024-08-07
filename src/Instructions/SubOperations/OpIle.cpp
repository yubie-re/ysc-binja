#include "inc.hpp"
#include "OpIle.hpp"

size_t OpIle::GetSize()
{
    return 1;
}

std::string_view OpIle::GetName()
{
    return "ILE";
}

bool OpIle::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.CompareSignedLessEqual(4, il.Pop(4), il.Pop(4))));
    return true;
}

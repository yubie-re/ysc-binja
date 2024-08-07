#include "inc.hpp"
#include "OpFsub.hpp"

size_t OpFsub::GetSize()
{
    return 1;
}

std::string_view OpFsub::GetName()
{
    return "FSUB";
}

bool OpFsub::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatSub(4, il.Pop(4), il.Pop(4))));
    return true;
}

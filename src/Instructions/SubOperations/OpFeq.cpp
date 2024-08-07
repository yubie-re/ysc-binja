#include "inc.hpp"
#include "OpFeq.hpp"

size_t OpFeq::GetSize()
{
    return 1;
}

std::string_view OpFeq::GetName()
{
    return "FEQ";
}

bool OpFeq::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Push(4, il.FloatCompareEqual(4, il.Pop(4), il.Pop(4))));
    return true;
}

#include "inc.hpp"
#include "OpCallindirect.hpp"

size_t OpCallindirect::GetSize()
{
    return 1;
}

std::string_view OpCallindirect::GetName()
{
    return "CALLINDIRECT";
}

bool OpCallindirect::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Call(il.Pop(4)));
    return true;
}

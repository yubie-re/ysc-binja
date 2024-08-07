#include "inc.hpp"
#include "OpStore.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpStore::GetSize()
{
    return 1;
}

std::string_view OpStore::GetName()
{
    return "STORE";
}

bool OpStore::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Store(4, il.Load(4, il.Register(4, Reg_SP)), il.Pop(4)));
    il.AddInstruction(il.Pop(4));
    return true;
}

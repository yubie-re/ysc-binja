#include "inc.hpp"
#include "OpNop.hpp"

size_t OpNop::GetSize()
{
    return 1;
}

std::string_view OpNop::GetName()
{
    return "NOP";
}

bool OpNop::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Nop());
    return true;
}

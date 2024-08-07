#include "inc.hpp"
#include "OpFmod.hpp"

size_t OpFmod::GetSize()
{
    return 1;
}

std::string_view OpFmod::GetName()
{
    return "FMOD";
}

bool OpFmod::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    auto divisor = il.Pop(4);
    auto dividend = il.Pop(4);
    auto quotient = il.FloatDiv(4, dividend, divisor);
    auto truncatedQuotient = il.FloatToInt(4, quotient);
    auto floatTruncatedQuotient = il.IntToFloat(4, truncatedQuotient);
    auto product = il.FloatMult(4, floatTruncatedQuotient, divisor);
    auto remainder = il.FloatSub(4, dividend, product);
    il.AddInstruction(il.Push(4, remainder));
    return true;
}

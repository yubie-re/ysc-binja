#include "inc.hpp"
#include "OpArrayU8Load.hpp"

size_t OpArrayU8Load::GetSize()
{
    return 2;
}

std::string_view OpArrayU8Load::GetName()
{
    return "ARRAY_U8_LOAD";
}

void OpArrayU8Load::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", data[0]), data[0]));
}

bool OpArrayU8Load::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    uint8_t num = data[0];
    auto r = il.Pop(4);
    auto idx = il.Pop(4);
    auto onePlusIdxTimesImm = il.Add(4, il.Const(4, 1), il.Mult(4, il.Const(4, num), idx));
    auto expr = il.Push(4, il.Load(4, il.Add(4, r, il.Mult(4, il.Const(4, 8), onePlusIdxTimesImm))));
    il.AddInstruction(expr);
    return true;
}

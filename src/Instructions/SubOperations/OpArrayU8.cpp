#include "inc.hpp"
#include "OpArrayU8.hpp"

size_t OpArrayU8::GetSize()
{
    return 2;
}

std::string_view OpArrayU8::GetName()
{
    return "ARRAY_U8";
}

void OpArrayU8::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", data[0]), data[0]));
}

bool OpArrayU8::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    uint8_t num = data[0];
    auto r = il.Pop(4);
    auto idx = il.Pop(4);
    auto one_plus_idx_times_imm = il.Add(4, il.Const(4, 1), il.Mult(4, il.Const(4, num), idx));
    auto expr = il.Push(4, il.ConstPointer(4, il.Add(4, r, il.Mult(
        4, il.Const(4, 8), one_plus_idx_times_imm))));
    il.AddInstruction(expr);
    return true;
}

#include "inc.hpp"
#include "OpArrayU16.hpp"

size_t OpArrayU16::GetSize()
{
    return 3;
}

std::string_view OpArrayU16::GetName()
{
    return "ARRAY_U16";
}

void OpArrayU16::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpArrayU16::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    uint16_t num = *reinterpret_cast<const uint16_t*>(data);
    auto r = il.Pop(4);
    auto idx = il.Pop(4);
    auto one_plus_idx_times_imm = il.Add(4, il.Const(4, 1), il.Mult(4, il.Const(4, num), idx));
    auto expr = il.Push(4, il.ConstPointer(4, il.Add(4, r, il.Mult(4, il.Const(4, 8), one_plus_idx_times_imm))));
    il.AddInstruction(expr);
    return true;
}

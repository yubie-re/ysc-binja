#include "inc.hpp"
#include "OpArrayU16Store.hpp"

size_t OpArrayU16Store::GetSize()
{
    return 3;
}

std::string_view OpArrayU16Store::GetName()
{
    return "ARRAY_U16_STORE";
}

void OpArrayU16Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpArrayU16Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    auto r = il.Pop(4);
    auto idx = il.Pop(4);
    auto onePlusIdxTimesImm = il.Add(4, il.Const(4, 1), il.Mult(4, il.Const(4, *reinterpret_cast<const uint16_t*>(data)), idx));
    auto newR = il.Add(4, r, il.Mult(
            4, il.Const(4, 8), onePlusIdxTimesImm));
    auto expr = il.Store(4, newR, il.Pop(4));
    return true;
}

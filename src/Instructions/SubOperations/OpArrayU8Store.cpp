#include "inc.hpp"
#include "OpArrayU8Store.hpp"

size_t OpArrayU8Store::GetSize()
{
    return 2;
}

std::string_view OpArrayU8Store::GetName()
{
    return "ARRAY_U8_STORE";
}

void OpArrayU8Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", data[0]), data[0]));
}

bool OpArrayU8Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    uint8_t num = data[0];
    auto r = il.Pop(4);
    auto idx = il.Pop(4);
    auto onePlusIdxTimesImm = il.Add(4, il.Const(4, 1), il.Mult(4, il.Const(4, num), idx));
    auto newR = il.Add(4, r, il.Mult(
            4, il.Const(4, 8), onePlusIdxTimesImm));
    auto expr = il.Store(4, newR, il.Pop(4));
    return true;
}

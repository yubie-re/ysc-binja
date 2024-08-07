#include "inc.hpp"
#include "OpCall.hpp"
#include "Uint24.hpp"

size_t OpCall::GetSize()
{
    return 4;
}

std::string_view OpCall::GetName()
{
    return "CALL";
}

void OpCall::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::PossibleAddressToken, fmt::format("{:x}", operand), operand));
}

bool OpCall::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    il.AddInstruction(il.Call(il.Const(4, operand)));
    return true;
}

bool OpCall::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result)
{
    OpBase::GetInstructionInfo(data, addr, maxLen, result);
    const uint32_t operand = Uint24(data);
    result.AddBranch(BNBranchType::CallDestination, operand);
    return true;
}

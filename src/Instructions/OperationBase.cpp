#include "inc.hpp"
#include "OperationBase.hpp"


void OpBase::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::InstructionToken, std::string(GetName())));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, " "));
}

bool OpBase::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.Unimplemented());
    return true;
}

bool OpBase::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::Ref<BinaryNinja::BasicBlock> block)
{
    result.length = GetSize();
    return true;
}
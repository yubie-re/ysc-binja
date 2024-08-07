#include "inc.hpp"
#include "OpTextLabelAssignInt.hpp"

size_t OpTextLabelAssignInt::GetSize()
{
    return 2;
}

std::string_view OpTextLabelAssignInt::GetName()
{
    return "TEXT_LABEL_ASSIGN_INT";
}

void OpTextLabelAssignInt::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = data[0];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpTextLabelAssignInt::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    // TODO
    il.AddInstruction(il.Pop(4));
    il.AddInstruction(il.Pop(4));
    return true;
}

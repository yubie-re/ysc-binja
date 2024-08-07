#include "inc.hpp"
#include "OpTextLabelAssignString.hpp"

size_t OpTextLabelAssignString::GetSize()
{
    return 2;
}

std::string_view OpTextLabelAssignString::GetName()
{
    return "TEXT_LABEL_ASSIGN_STRING";
}

void OpTextLabelAssignString::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = data[0];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpTextLabelAssignString::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    // TODO
    il.AddInstruction(il.Pop(4));
    il.AddInstruction(il.Pop(4));
    return true;
}

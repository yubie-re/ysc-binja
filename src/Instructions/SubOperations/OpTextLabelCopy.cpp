#include "inc.hpp"
#include "OpTextLabelCopy.hpp"

size_t OpTextLabelCopy::GetSize()
{
    return 2;
}

std::string_view OpTextLabelCopy::GetName()
{
    return "TEXT_LABEL_COPY";
}

void OpTextLabelCopy::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = data[0];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpTextLabelCopy::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    // TODO
    il.AddInstruction(il.Pop(4));
    il.AddInstruction(il.Pop(4));
    il.AddInstruction(il.Pop(4));
    return true;
}

#include "inc.hpp"
#include "OpTextLabelAppendInt.hpp"

size_t OpTextLabelAppendInt::GetSize()
{
    return 2;
}

std::string_view OpTextLabelAppendInt::GetName()
{
    return "TEXT_LABEL_APPEND_INT";
}

void OpTextLabelAppendInt::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = data[0];
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpTextLabelAppendInt::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    // TODO
    il.AddInstruction(il.Pop(4));
    il.AddInstruction(il.Pop(4));
    return true;
}

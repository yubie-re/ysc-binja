#include "inc.hpp"
#include "OpTextLabelCopy.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpTextLabelCopy::GetSize()
{
    return 1;
}

std::string_view OpTextLabelCopy::GetName()
{
    return "TEXT_LABEL_COPY";
}

void OpTextLabelCopy::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    OpBase::GetInstructionText(data, addr, len, result);
}

bool OpTextLabelCopy::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    return EmitYSCTextLabelFallbackLLIL(OP_TEXT_LABEL_COPY, data, len, il);
}

#include "inc.hpp"
#include "OpStaticU24Store.hpp"
#include "Uint24.hpp"

size_t OpStaticU24Store::GetSize()
{
    return 4;
}

std::string_view OpStaticU24Store::GetName()
{
    return "STATIC_U24_STORE";
}

void OpStaticU24Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpStaticU24Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    if(!il.GetFunction())
        return false;
    if(!il.GetFunction()->GetView())
        return false;
    if(!il.GetFunction()->GetView()->GetSectionByName("STATICS"))
        return false;

    il.AddInstruction(il.Store(4, il.Add(4, il.Const(4, 8 * operand), il.Const(4, il.GetFunction()->GetView()->GetSectionByName("STATICS")->GetStart())), il.Pop(4)));
    return true;
}

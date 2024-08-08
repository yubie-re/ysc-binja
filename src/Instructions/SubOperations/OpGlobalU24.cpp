#include "inc.hpp"
#include "OpGlobalU24.hpp"
#include "Uint24.hpp"

size_t OpGlobalU24::GetSize()
{
    return 4;
}

std::string_view OpGlobalU24::GetName()
{
    return "GLOBAL_U24";
}

void OpGlobalU24::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint32_t operand = Uint24(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpGlobalU24::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = Uint24(data);
    if(!il.GetFunction())
        return false;
    if(!il.GetFunction()->GetView())
        return false;
    if(!il.GetFunction()->GetView()->GetSectionByName("GLOBALS"))
        return false;
    uint32_t block = operand >> 18;
    uint32_t blockSize = 1 << 18;
    uint32_t needle = operand & 0x3FFFF;
    auto view = il.GetFunction()->GetView();
    uint32_t virtualAddress = view->GetSectionByName("GLOBALS")->GetStart() + blockSize * block + needle;
    view->DefineDataVariable(virtualAddress, BinaryNinja::Type::IntegerType(8, true));
    view->DefineAutoSymbol(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, fmt::format("Global_{}", operand), virtualAddress));
    il.AddInstruction(il.Push(4, il.ConstPointer(4, virtualAddress)));
    return true;
}

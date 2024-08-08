#include "inc.hpp"
#include "OpGlobalU16Store.hpp"

size_t OpGlobalU16Store::GetSize()
{
    return 3;
}

std::string_view OpGlobalU16Store::GetName()
{
    return "GLOBAL_U16_STORE";
}

void OpGlobalU16Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint16_t operand = *reinterpret_cast<const uint16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpGlobalU16Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint32_t operand = static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(data));
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
    il.AddInstruction(il.Store(4, il.Const(4, virtualAddress), il.Pop(4)));
    return true;
}

#include "inc.hpp"
#include "OpIoffsetU8Store.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpIoffsetU8Store::GetSize()
{
    return 2;
}

std::string_view OpIoffsetU8Store::GetName()
{
    return "IOFFSET_U8_STORE";
}

void OpIoffsetU8Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpIoffsetU8Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t operand = *reinterpret_cast<const uint8_t*>(data);
    il.AddInstruction(il.SetRegister(4, Reg_R1, il.Pop(4)));
    il.AddInstruction(
        il.Store(4, 
            il.Add(4, 
                il.Register(4, Reg_R1), 
                il.Const(4, static_cast<int>(operand) * 8)), 
        il.Pop(4)));
    return true;
}

#include "inc.hpp"
#include "OpIoffsetS16Store.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpIoffsetS16Store::GetSize()
{
    return 3;
}

std::string_view OpIoffsetS16Store::GetName()
{
    return "IOFFSET_S16_STORE";
}

void OpIoffsetS16Store::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:x}", operand), operand));
}

bool OpIoffsetS16Store::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const int16_t operand = *reinterpret_cast<const int16_t*>(data);
    il.AddInstruction(il.SetRegister(4, Reg_POPHOLDER, il.Pop(4)));
    il.AddInstruction(
        il.Store(4, 
            il.Add(4, 
                il.Register(4, Reg_POPHOLDER), 
                il.Const(4, static_cast<int>(operand) * 8)), 
        il.Pop(4)));
    return true;
}

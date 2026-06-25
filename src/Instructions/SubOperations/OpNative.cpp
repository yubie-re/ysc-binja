#include "inc.hpp"
#include "OpNative.hpp"
#include "Architecture/YSCArchitecture.hpp"

size_t OpNative::GetSize()
{
    return 4;
}

std::string_view OpNative::GetName()
{
    return "NATIVE";
}

void OpNative::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    const uint8_t retSize = data[0] & 3;
    const uint8_t paramCount = (data[0] >> 2) & 63;
    const int nativeOffset = static_cast<int>((data[1] << 8) | data[2]) * 8;
    OpBase::GetInstructionText(data, addr, len, result);
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", retSize), retSize));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, ", "));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", paramCount), paramCount));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::OperandSeparatorToken, ", "));
    result.push_back(BinaryNinja::InstructionTextToken(BNInstructionTextTokenType::IntegerToken, fmt::format("{:#x}", nativeOffset), nativeOffset));
}

bool OpNative::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    const uint8_t retSize = data[0] & 3;
    const uint8_t paramCount = (data[0] >> 2) & 63;
    const int nativeOffset = static_cast<int>((data[1] << 8) | data[2]) * 8;
    if(il.GetFunction() && il.GetFunction()->GetView())
    {
        if(auto section = il.GetFunction()->GetView()->GetSectionByName("NATIVES"))
        {
            uint64_t nativeAddress = section->GetStart() + nativeOffset;
            il.AddInstruction(il.CallStackAdjust(il.ExternPointer(8, nativeAddress, 0), 4*(paramCount - retSize), {}));

            if (auto symbol = il.GetFunction()->GetView()->GetSymbolByAddress(nativeAddress))
            {
                if (symbol->GetRawName() == "native_SCRIPT_TERMINATE_THIS_THREAD")
                    il.AddInstruction(il.NoReturn());
            }
        }
        else
            return false;
    }
    else
        return false;
    return true;
}

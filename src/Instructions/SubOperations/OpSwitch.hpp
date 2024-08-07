#ifndef SWITCH_HPP
#define SWITCH_HPP

#include "../OperationBase.hpp"

#pragma pack(push, 1)
struct SwitchCase
{
    uint32_t m_case;
    uint16_t m_target;
};
static_assert(sizeof(SwitchCase) == 6);
#pragma pack(pop)

class OpSwitch : public OpBase
{
public:
    size_t GetSize() override;
    std::string_view GetName() override;
    void GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len, std::vector<BinaryNinja::InstructionTextToken>& result) override;
    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;
    bool GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result) override;
private:
    void ProcessSwitchCases(std::vector<SwitchCase> switchData, BinaryNinja::LowLevelILFunction& il, int switchCount, uint64_t address, int index = 0);
};

#endif

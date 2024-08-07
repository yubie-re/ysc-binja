#ifndef YSC_ARCHITECTURE
#define YSC_ARCHITECTURE

#include "Instructions/OperationEnum.hpp"
#include "Instructions/OperationBase.hpp"
#include <array>
#include <memory>

enum Registers
{
    Reg_SP,
    Reg_FP,
    Reg_TMP,
    Reg_RETADDR,
    Reg_SWITCH,
    Reg_VX1,
    Reg_VY1,
    Reg_VZ1,
    Reg_VX2,
    Reg_VY2,
    Reg_VZ2,
    Reg_MAX
};

const std::array<std::string_view, Reg_MAX> g_RegNames = {
    "SP",
    "FP",
    "TMP",
    "RETADDR",
    "SWITCH",
    "VX1",
    "VY1",
    "VZ1",
    "VX2",
    "VY2",
    "VZ2"
};

class YSCArchitecture : public BinaryNinja::Architecture
{
public:
    YSCArchitecture(const std::string& name);

    BNEndianness GetEndianness() const override { return BNEndianness::LittleEndian; }

    size_t GetAddressSize() const override { return 4; };

    size_t GetInstructionAlignment() const override { return 1; };

	size_t GetDefaultIntegerSize() const override { return 4; };

	size_t GetMaxInstructionLength() const override { return 10; };

	std::string GetRegisterName(uint32_t reg) override;

	bool GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen,BinaryNinja::InstructionInfo& result) override;

	bool GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len,std::vector<BinaryNinja::InstructionTextToken>& result) override;

	bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il) override;

	BNRegisterInfo GetRegisterInfo(uint32_t reg) override;

	uint32_t GetStackPointerRegister() override;

	std::vector<uint32_t> GetAllRegisters() override;
private:
    std::array<std::unique_ptr<OpBase>, OP_MAX> m_insns;
};

#endif
#ifndef YSC_ARCHITECTURE
#define YSC_ARCHITECTURE

#include "Architecture/BlockAnalysisContext.hpp"
#include "Architecture/FunctionContext.hpp"
#include "Instructions/OperationEnum.hpp"
#include "Architecture/Intrinsics.hpp"
#include "Architecture/Registers.hpp"

uintptr_t MarkYSCArchitectureViewActive(BinaryNinja::BinaryView* view);
void RetireYSCArchitectureView(uintptr_t key);

bool EmitYSCTextLabelFallbackLLIL(uint8_t opcode, const uint8_t* data, size_t& len,
                                  BinaryNinja::LowLevelILFunction& il);

constexpr size_t YSC_MAX_INSTRUCTION_LENGTH = BN_MAX_INSTRUCTION_LENGTH;
constexpr size_t YSC_MAX_INTERNAL_INSTRUCTION_LENGTH = 2 + 255 * 6;

class YSCArchitecture : public BinaryNinja::ArchitectureWithFunctionContext<YSCFunctionContext>
{
  public:
    YSCArchitecture(const std::string& name);

    BNEndianness GetEndianness() const override
    {
        return BNEndianness::LittleEndian;
    }

    size_t GetAddressSize() const override
    {
        return 4;
    };

    size_t GetInstructionAlignment() const override
    {
        return 1;
    };

    size_t GetDefaultIntegerSize() const override
    {
        return 4;
    };

    size_t GetMaxInstructionLength() const override
    {
        return YSC_MAX_INSTRUCTION_LENGTH;
    };

    std::string GetRegisterName(uint32_t reg) override;

    bool GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen, BinaryNinja::InstructionInfo& result) override;

    bool GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len,
                            std::vector<BinaryNinja::InstructionTextToken>& result) override;

    bool GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len,
                                  BinaryNinja::LowLevelILFunction& il) override;

    BNRegisterInfo GetRegisterInfo(uint32_t reg) override;

    uint32_t GetStackPointerRegister() override;

    std::vector<uint32_t> GetAllRegisters() override;

    BNIntrinsicClass GetIntrinsicClass(uint32_t intrinsic) override;

    std::string GetIntrinsicName(uint32_t intrinsic) override;

    std::vector<uint32_t> GetAllIntrinsics() override;

    std::vector<BinaryNinja::NameAndType> GetIntrinsicInputs(uint32_t intrinsic) override;

    std::vector<BinaryNinja::Confidence<BinaryNinja::Ref<BinaryNinja::Type>>>
    GetIntrinsicOutputs(uint32_t intrinsic) override;

    void AnalyzeBasicBlocks(BinaryNinja::Function* function, BinaryNinja::BasicBlockAnalysisContext& context) override;

    bool LiftFunction(BinaryNinja::LowLevelILFunction* function, BinaryNinja::FunctionLifterContext& context) override;

    void FreeFunctionArchContext(YSCFunctionContext* context) override;
};

#endif

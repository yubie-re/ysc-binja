#ifndef YSC_SYMBOLIC_LIFTER_HPP
#define YSC_SYMBOLIC_LIFTER_HPP

#include "inc.hpp"
#include <memory>
#include <optional>
#include <unordered_set>

class YSCArchitecture;

class YSCSymbolicLifter
{
  public:
    YSCSymbolicLifter(YSCArchitecture* arch, BinaryNinja::LowLevelILFunction& il, BinaryNinja::BinaryView* view);
    ~YSCSymbolicLifter();

    bool Lift(const uint8_t* opcode, uint64_t addr, size_t& len);
    void DisableAndFlush();
    void Reset();
    void SetExpectedOutgoingStackDepth(std::optional<size_t> depth, std::vector<uint32_t> targetBlockIndices);
    void SetExpectedBranchStackTargets(std::optional<size_t> depth, std::optional<uint32_t> fallthroughBlockIndex,
                                       std::optional<uint32_t> branchBlockIndex, bool explicitFallthroughGoto);
    void SeedStack(uint32_t blockIndex, size_t depth);
    void SetValidInstructionStarts(std::unordered_set<uint64_t> instructionStarts);
    bool StoreStackOutputs(uint32_t targetBlockIndex, size_t expectedDepth);
    bool StoreStackOutputs(size_t expectedDepth);
    size_t StackDepth() const;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif

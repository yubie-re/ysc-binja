#include "inc.hpp"
#include "Architecture/YSCArchitecture.hpp"
#include "Common/Uint24.hpp"
#include "Instructions/InstructionRegistry.hpp"
#include "Instructions/SwitchCase.hpp"
#include <array>
#include <cstring>

namespace
{
enum class OperandFormat
{
    None,
    U8,
    U16,
    U24,
    U32,
    S16,
    Float,
    PushU8U8,
    PushU8U8U8,
    PushSmallInt,
    PushSmallFloat,
    Enter,
    Leave,
    Branch,
    Call,
    Native,
    Switch,
    Global,
};

struct InstructionDescriptor
{
    std::string_view name;
    size_t size;
    OperandFormat format = OperandFormat::None;
};

template <typename T>
T ReadUnaligned(const uint8_t* data)
{
    T result {};
    std::memcpy(&result, data, sizeof(T));
    return result;
}

uint64_t BranchTarget(uint64_t addr, const uint8_t* data)
{
    return static_cast<uint64_t>(static_cast<int64_t>(addr) + static_cast<int16_t>(ReadUnaligned<int16_t>(data)) + 3);
}

uint32_t ReadU24(const uint8_t* data)
{
    return Uint24(data);
}

void AddName(std::string_view name, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    result.emplace_back(BNInstructionTextTokenType::InstructionToken, std::string(name));
    result.emplace_back(BNInstructionTextTokenType::OperandSeparatorToken, " ");
}

void AddInt(uint64_t value, std::vector<BinaryNinja::InstructionTextToken>& result, bool prefix = false)
{
    result.emplace_back(BNInstructionTextTokenType::IntegerToken, prefix ? fmt::format("{:#x}", value) : fmt::format("{:x}", value), value);
}

void AddSep(std::vector<BinaryNinja::InstructionTextToken>& result)
{
    result.emplace_back(BNInstructionTextTokenType::OperandSeparatorToken, ", ");
}

void AddGlobalOperandText(uint32_t operand, std::vector<BinaryNinja::InstructionTextToken>& result)
{
    uint32_t block = operand >> 18;
    uint32_t offset = operand & 0x3ffff;
    AddInt(operand, result);
    result.emplace_back(BNInstructionTextTokenType::TextToken,
                        fmt::format(" <Global_{} block={} offset={:#x}>", operand, block, offset));
}

const std::array<InstructionDescriptor, OP_MAX>& InstructionDescriptors()
{
    static const std::array<InstructionDescriptor, OP_MAX> descriptors = {{
        {"NOP", 1},
        {"IADD", 1},
        {"ISUB", 1},
        {"IMUL", 1},
        {"IDIV", 1},
        {"IMOD", 1},
        {"INOT", 1},
        {"INEG", 1},
        {"IEQ", 1},
        {"INE", 1},
        {"IGT", 1},
        {"IGE", 1},
        {"ILT", 1},
        {"ILE", 1},
        {"FADD", 1},
        {"FSUB", 1},
        {"FMUL", 1},
        {"FDIV", 1},
        {"FMOD", 1},
        {"FNEG", 1},
        {"FEQ", 1},
        {"FNE", 1},
        {"FGT", 1},
        {"FGE", 1},
        {"FLT", 1},
        {"FLE", 1},
        {"VADD", 1},
        {"VSUB", 1},
        {"VMUL", 1},
        {"VDIV", 1},
        {"VNEG", 1},
        {"IAND", 1},
        {"IOR", 1},
        {"IXOR", 1},
        {"I2F", 1},
        {"F2I", 1},
        {"F2V", 1},
        {"PUSH_CONST_U8", 2, OperandFormat::U8},
        {"PUSH_CONST_U8_U8", 3, OperandFormat::PushU8U8},
        {"PUSH_CONST_U8_U8_U8", 4, OperandFormat::PushU8U8U8},
        {"PUSH_CONST_U32", 5, OperandFormat::U32},
        {"PUSH_CONST_F", 5, OperandFormat::Float},
        {"DUP", 1},
        {"DROP", 1},
        {"NATIVE", 4, OperandFormat::Native},
        {"ENTER", 5, OperandFormat::Enter},
        {"LEAVE", 3, OperandFormat::Leave},
        {"LOAD", 1},
        {"STORE", 1},
        {"STORE_REV", 1},
        {"LOAD_N", 1},
        {"STORE_N", 1},
        {"ARRAY_U8", 2, OperandFormat::U8},
        {"ARRAY_U8_LOAD", 2, OperandFormat::U8},
        {"ARRAY_U8_STORE", 2, OperandFormat::U8},
        {"LOCAL_U8", 2, OperandFormat::U8},
        {"LOCAL_U8_LOAD", 2, OperandFormat::U8},
        {"LOCAL_U8_STORE", 2, OperandFormat::U8},
        {"STATIC_U8", 2, OperandFormat::U8},
        {"STATIC_U8_LOAD", 2, OperandFormat::U8},
        {"STATIC_U8_STORE", 2, OperandFormat::U8},
        {"IADD_U8", 2, OperandFormat::U8},
        {"IMUL_U8", 2, OperandFormat::U8},
        {"IOFFSET", 1},
        {"IOFFSET_U8", 2, OperandFormat::U8},
        {"IOFFSET_U8_LOAD", 2, OperandFormat::U8},
        {"IOFFSET_U8_STORE", 2, OperandFormat::U8},
        {"PUSH_CONST_S16", 3, OperandFormat::S16},
        {"IADD_S16", 3, OperandFormat::S16},
        {"IMUL_S16", 3, OperandFormat::S16},
        {"IOFFSET_S16", 3, OperandFormat::S16},
        {"IOFFSET_S16_LOAD", 3, OperandFormat::S16},
        {"IOFFSET_S16_STORE", 3, OperandFormat::S16},
        {"ARRAY_U16", 3, OperandFormat::U16},
        {"ARRAY_U16_LOAD", 3, OperandFormat::U16},
        {"ARRAY_U16_STORE", 3, OperandFormat::U16},
        {"LOCAL_U16", 3, OperandFormat::U16},
        {"LOCAL_U16_LOAD", 3, OperandFormat::U16},
        {"LOCAL_U16_STORE", 3, OperandFormat::U16},
        {"STATIC_U16", 3, OperandFormat::U16},
        {"STATIC_U16_LOAD", 3, OperandFormat::U16},
        {"STATIC_U16_STORE", 3, OperandFormat::U16},
        {"GLOBAL_U16", 3, OperandFormat::Global},
        {"GLOBAL_U16_LOAD", 3, OperandFormat::Global},
        {"GLOBAL_U16_STORE", 3, OperandFormat::Global},
        {"J", 3, OperandFormat::Branch},
        {"JZ", 3, OperandFormat::Branch},
        {"IEQ_JZ", 3, OperandFormat::Branch},
        {"INE_JZ", 3, OperandFormat::Branch},
        {"IGT_JZ", 3, OperandFormat::Branch},
        {"IGE_JZ", 3, OperandFormat::Branch},
        {"ILT_JZ", 3, OperandFormat::Branch},
        {"ILE_JZ", 3, OperandFormat::Branch},
        {"CALL", 4, OperandFormat::Call},
        {"STATIC_U24", 4, OperandFormat::U24},
        {"STATIC_U24_LOAD", 4, OperandFormat::U24},
        {"STATIC_U24_STORE", 4, OperandFormat::U24},
        {"GLOBAL_U24", 4, OperandFormat::Global},
        {"GLOBAL_U24_LOAD", 4, OperandFormat::Global},
        {"GLOBAL_U24_STORE", 4, OperandFormat::Global},
        {"PUSH_CONST_U24", 4, OperandFormat::U24},
        {"SWITCH", 2, OperandFormat::Switch},
        {"STRING", 1},
        {"STRINGHASH", 1},
        {"TEXT_LABEL_ASSIGN_STRING", 2, OperandFormat::U8},
        {"TEXT_LABEL_ASSIGN_INT", 2, OperandFormat::U8},
        {"TEXT_LABEL_APPEND_STRING", 2, OperandFormat::U8},
        {"TEXT_LABEL_APPEND_INT", 2, OperandFormat::U8},
        {"TEXT_LABEL_COPY", 1},
        {"CATCH", 1},
        {"THROW", 1},
        {"CALLINDIRECT", 1},
        {"PUSH_CONST_M1", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_0", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_1", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_2", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_3", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_4", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_5", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_6", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_7", 1, OperandFormat::PushSmallInt},
        {"PUSH_CONST_FM1", 1, OperandFormat::PushSmallFloat},
        {"PUSH_CONST_F0", 1, OperandFormat::PushSmallFloat},
        {"PUSH_CONST_F1", 1, OperandFormat::PushSmallFloat},
        {"PUSH_CONST_F2", 1, OperandFormat::PushSmallFloat},
        {"PUSH_CONST_F3", 1, OperandFormat::PushSmallFloat},
        {"PUSH_CONST_F4", 1, OperandFormat::PushSmallFloat},
        {"PUSH_CONST_F5", 1, OperandFormat::PushSmallFloat},
        {"PUSH_CONST_F6", 1, OperandFormat::PushSmallFloat},
        {"PUSH_CONST_F7", 1, OperandFormat::PushSmallFloat},
        {"IS_BIT_SET", 1},
    }};
    return descriptors;
}

const InstructionDescriptor* GetDescriptor(uint8_t opcode)
{
    if (opcode >= OP_MAX)
        return nullptr;
    return &InstructionDescriptors()[opcode];
}

bool IsConditionalBranchOpcode(uint8_t opcode)
{
    return opcode == OP_JZ || opcode == OP_IEQ_JZ || opcode == OP_INE_JZ || opcode == OP_IGT_JZ ||
        opcode == OP_IGE_JZ || opcode == OP_ILT_JZ || opcode == OP_ILE_JZ;
}

uint32_t ReadOperandByFormat(const uint8_t* data, OperandFormat format)
{
    switch (format)
    {
    case OperandFormat::U8:
    case OperandFormat::Global:
        return data[0];
    case OperandFormat::U16:
        return ReadUnaligned<uint16_t>(data);
    case OperandFormat::U24:
        return ReadU24(data);
    case OperandFormat::U32:
        return ReadUnaligned<uint32_t>(data);
    case OperandFormat::S16:
        return static_cast<uint32_t>(ReadUnaligned<int16_t>(data));
    default:
        return 0;
    }
}
}

bool GetYSCInstructionInfo(uint8_t opcode, const uint8_t* data, uint64_t addr, size_t maxLen,
                           BinaryNinja::InstructionInfo& result)
{
    auto descriptor = GetDescriptor(opcode);
    if (!descriptor)
        return false;

    result.length = descriptor->size;
    if (opcode == OP_J)
    {
        result.AddBranch(BNBranchType::UnconditionalBranch, BranchTarget(addr, data));
    }
    else if (IsConditionalBranchOpcode(opcode))
    {
        result.AddBranch(BNBranchType::TrueBranch, addr + 3);
        result.AddBranch(BNBranchType::FalseBranch, BranchTarget(addr, data));
    }
    else if (opcode == OP_LEAVE)
    {
        result.AddBranch(BNBranchType::FunctionReturn);
    }
    else if (opcode == OP_THROW)
    {
        result.AddBranch(BNBranchType::UnresolvedBranch);
    }
    else if (opcode == OP_SWITCH)
    {
        if (maxLen >= 2)
            result.length = 2 + static_cast<size_t>(data[0]) * sizeof(SwitchCase);
        result.AddBranch(BNBranchType::UnresolvedBranch);
    }
    return true;
}

void GetYSCInstructionText(uint8_t opcode, const uint8_t* data, uint64_t addr, size_t& len,
                           std::vector<BinaryNinja::InstructionTextToken>& result)
{
    auto descriptor = GetDescriptor(opcode);
    if (!descriptor)
        return;

    AddName(descriptor->name, result);
    switch (descriptor->format)
    {
    case OperandFormat::None:
        return;
    case OperandFormat::U8:
    case OperandFormat::U16:
    case OperandFormat::U24:
    case OperandFormat::U32:
    case OperandFormat::S16:
        AddInt(ReadOperandByFormat(data, descriptor->format), result);
        return;
    case OperandFormat::Float:
    {
        float operand = ReadUnaligned<float>(data);
        result.emplace_back(BNInstructionTextTokenType::IntegerToken, fmt::format("{:.1f}", operand), operand);
        return;
    }
    case OperandFormat::PushU8U8:
        AddInt(data[0], result, true);
        AddSep(result);
        AddInt(data[1], result, true);
        return;
    case OperandFormat::PushU8U8U8:
        AddInt(data[0], result, true);
        AddSep(result);
        AddInt(data[1], result, true);
        AddSep(result);
        AddInt(data[2], result, true);
        return;
    case OperandFormat::PushSmallInt:
        AddInt(opcode == OP_PUSH_CONST_M1 ? static_cast<uint32_t>(-1) : opcode - OP_PUSH_CONST_0, result);
        return;
    case OperandFormat::PushSmallFloat:
    {
        float value = opcode == OP_PUSH_CONST_FM1 ? -1.0f : static_cast<float>(opcode - OP_PUSH_CONST_F0);
        result.emplace_back(BNInstructionTextTokenType::IntegerToken, fmt::format("{:.1f}", value), value);
        return;
    }
    case OperandFormat::Enter:
    {
        uint8_t paramCount = data[0];
        uint16_t localCount = ReadUnaligned<uint16_t>(data + 1);
        uint8_t nameCount = data[3];
        len = 5 + nameCount;
        AddInt(paramCount, result, true);
        AddSep(result);
        AddInt(localCount, result, true);
        if (nameCount > 0)
        {
            AddSep(result);
            std::string_view name(reinterpret_cast<const char*>(data + 4), nameCount);
            result.emplace_back(BNInstructionTextTokenType::StringToken, fmt::format("\"{}\"", name), 0);
        }
        return;
    }
    case OperandFormat::Leave:
        AddInt(data[0], result, true);
        AddSep(result);
        AddInt(data[1], result, true);
        return;
    case OperandFormat::Branch:
    {
        uint64_t target = BranchTarget(addr, data);
        result.emplace_back(BNInstructionTextTokenType::PossibleAddressToken, fmt::format("{:x}", target), target);
        return;
    }
    case OperandFormat::Call:
    {
        uint64_t target = ReadU24(data) + CODE_OFFSET;
        result.emplace_back(BNInstructionTextTokenType::PossibleAddressToken, fmt::format("{:x}", target), target);
        return;
    }
    case OperandFormat::Native:
    {
        uint8_t retSize = data[0] & 3;
        uint8_t paramCount = (data[0] >> 2) & 63;
        uint64_t nativeOffset = static_cast<uint64_t>((data[1] << 8) | data[2]) * 8;
        AddInt(retSize, result, true);
        AddSep(result);
        AddInt(paramCount, result, true);
        AddSep(result);
        AddInt(nativeOffset, result, true);
        return;
    }
    case OperandFormat::Switch:
        len = 2 + static_cast<size_t>(data[0]) * sizeof(SwitchCase);
        AddInt(data[0], result);
        return;
    case OperandFormat::Global:
        AddGlobalOperandText(ReadOperandByFormat(data, descriptor->size == 3 ? OperandFormat::U16 : OperandFormat::U24), result);
        return;
    }
}

bool GetYSCInstructionLowLevelIL(uint8_t opcode, const uint8_t* data, uint64_t addr, size_t& len,
                                 BinaryNinja::LowLevelILFunction& il)
{
    switch (opcode)
    {
    case OP_TEXT_LABEL_ASSIGN_STRING:
    case OP_TEXT_LABEL_ASSIGN_INT:
    case OP_TEXT_LABEL_APPEND_STRING:
    case OP_TEXT_LABEL_APPEND_INT:
    case OP_TEXT_LABEL_COPY:
        return EmitYSCTextLabelFallbackLLIL(opcode, data, len, il);
    default:
        return false;
    }
}

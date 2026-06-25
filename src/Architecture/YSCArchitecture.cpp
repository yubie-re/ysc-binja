#include "inc.hpp"
#include "YSCArchitecture.hpp"
#include "Instructions/OperationEnum.hpp"
#include "Instructions/SubOperations/OpSwitch.hpp"
#include "Uint24.hpp"
#include "lowlevelilinstruction.h"
#include <map>
#include <mutex>
#include <queue>
#include <set>

namespace
{
constexpr uint32_t YSC_LOCAL_TEMP_BASE = 0x10000;
constexpr uint32_t YSC_STACK_TEMP_BASE = 0x20000;
constexpr uint32_t YSC_CALL_RESULT_TEMP_BASE = 0x80000;
constexpr uint64_t YSC_WHOLE_FUNCTION_LIFT_SIZE_LIMIT = 2048;
constexpr size_t YSC_STACK_ANALYSIS_WORK_LIMIT = 4096;

struct YSCLiftDiagnostic
{
    std::string reason = "ok";
    uint64_t address = 0;
    uint64_t target = 0;
    uint8_t opcode = OP_MAX;
    int depth = 0;
    int otherDepth = 0;
    size_t totalSize = 0;
};

const char* OpcodeName(uint8_t opcode)
{
    static constexpr std::array<std::string_view, OP_MAX> names = {
        "NOP", "IADD", "ISUB", "IMUL", "IDIV", "IMOD", "INOT", "INEG", "IEQ", "INE", "IGT", "IGE",
        "ILT", "ILE", "FADD", "FSUB", "FMUL", "FDIV", "FMOD", "FNEG", "FEQ", "FNE", "FGT", "FGE",
        "FLT", "FLE", "VADD", "VSUB", "VMUL", "VDIV", "VNEG", "IAND", "IOR", "IXOR", "I2F", "F2I",
        "F2V", "PUSH_CONST_U8", "PUSH_CONST_U8_U8", "PUSH_CONST_U8_U8_U8", "PUSH_CONST_U32",
        "PUSH_CONST_F", "DUP", "DROP", "NATIVE", "ENTER", "LEAVE", "LOAD", "STORE", "STORE_REV", "LOAD_N",
        "STORE_N", "ARRAY_U8", "ARRAY_U8_LOAD", "ARRAY_U8_STORE", "LOCAL_U8", "LOCAL_U8_LOAD",
        "LOCAL_U8_STORE", "STATIC_U8", "STATIC_U8_LOAD", "STATIC_U8_STORE", "IADD_U8", "IMUL_U8", "IOFFSET",
        "IOFFSET_U8", "IOFFSET_U8_LOAD", "IOFFSET_U8_STORE", "PUSH_CONST_S16", "IADD_S16", "IMUL_S16",
        "IOFFSET_S16", "IOFFSET_S16_LOAD", "IOFFSET_S16_STORE", "ARRAY_U16", "ARRAY_U16_LOAD",
        "ARRAY_U16_STORE", "LOCAL_U16", "LOCAL_U16_LOAD", "LOCAL_U16_STORE", "STATIC_U16", "STATIC_U16_LOAD",
        "STATIC_U16_STORE", "GLOBAL_U16", "GLOBAL_U16_LOAD", "GLOBAL_U16_STORE", "J", "JZ", "IEQ_JZ", "INE_JZ",
        "IGT_JZ", "IGE_JZ", "ILT_JZ", "ILE_JZ", "CALL", "STATIC_U24", "STATIC_U24_LOAD", "STATIC_U24_STORE",
        "GLOBAL_U24", "GLOBAL_U24_LOAD", "GLOBAL_U24_STORE", "PUSH_CONST_U24", "SWITCH", "STRING", "STRINGHASH",
        "TEXT_LABEL_ASSIGN_STRING", "TEXT_LABEL_ASSIGN_INT", "TEXT_LABEL_APPEND_STRING", "TEXT_LABEL_APPEND_INT",
        "TEXT_LABEL_COPY", "CATCH", "THROW", "CALLINDIRECT", "PUSH_CONST_M1", "PUSH_CONST_0", "PUSH_CONST_1",
        "PUSH_CONST_2", "PUSH_CONST_3", "PUSH_CONST_4", "PUSH_CONST_5", "PUSH_CONST_6", "PUSH_CONST_7",
        "PUSH_CONST_FM1", "PUSH_CONST_F0", "PUSH_CONST_F1", "PUSH_CONST_F2", "PUSH_CONST_F3", "PUSH_CONST_F4",
        "PUSH_CONST_F5", "PUSH_CONST_F6", "PUSH_CONST_F7", "IS_BIT_SET"
    };
    if (opcode < names.size())
        return names[opcode].data();
    return "INVALID";
}

template <typename T>
T ReadUnaligned(const uint8_t* data)
{
    T result {};
    std::memcpy(&result, data, sizeof(T));
    return result;
}

uint64_t BranchTarget(uint64_t addr, const uint8_t* opcode)
{
    return static_cast<uint64_t>(static_cast<int64_t>(addr) + static_cast<int16_t>(ReadUnaligned<int16_t>(opcode + 1)) + 3);
}

uint32_t DecodeU24(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[1]) << 8) | data[0];
}

bool IsEnterAt(BinaryNinja::BinaryView* view, uint64_t addr)
{
    uint8_t op = 0;
    return view && view->Read(&op, addr, 1) == 1 && op == OP_ENTER;
}

uint8_t GetEnterParamCount(BinaryNinja::BinaryView* view, uint64_t addr)
{
    uint8_t data[5] = {};
    if (!IsEnterAt(view, addr) || view->Read(data, addr, sizeof(data)) < sizeof(data))
        return 0;
    return data[1];
}

uint8_t FindFirstLeaveReturnCount(BinaryNinja::BinaryView* view, uint64_t addr)
{
    if (!view || !IsEnterAt(view, addr))
        return 0;

    auto code = view->GetSectionByName("CODE");
    uint64_t codeEnd = code ? code->GetEnd() : view->GetEnd();
    uint64_t cursor = addr;
    for (size_t i = 0; i < 4096 && cursor < codeEnd; i++)
    {
        uint8_t data[16] = {};
        if (view->Read(data, cursor, sizeof(data)) < 1)
            return 0;

        uint8_t op = data[0];
        if (op == OP_LEAVE)
        {
            if (view->Read(data, cursor, 3) == 3)
                return data[2];
            return 0;
        }

        if (op == OP_ENTER && cursor != addr)
            return 0;

        size_t size = 1;
        switch (op)
        {
        case OP_PUSH_CONST_U8:
        case OP_ARRAY_U8:
        case OP_ARRAY_U8_LOAD:
        case OP_ARRAY_U8_STORE:
        case OP_LOCAL_U8:
        case OP_LOCAL_U8_LOAD:
        case OP_LOCAL_U8_STORE:
        case OP_STATIC_U8:
        case OP_STATIC_U8_LOAD:
        case OP_STATIC_U8_STORE:
        case OP_IADD_U8:
        case OP_IMUL_U8:
        case OP_IOFFSET_U8:
        case OP_IOFFSET_U8_LOAD:
        case OP_IOFFSET_U8_STORE:
        case OP_TEXT_LABEL_ASSIGN_STRING:
        case OP_TEXT_LABEL_ASSIGN_INT:
        case OP_TEXT_LABEL_APPEND_STRING:
        case OP_TEXT_LABEL_APPEND_INT:
            size = 2;
            break;
        case OP_PUSH_CONST_U8_U8:
        case OP_PUSH_CONST_S16:
        case OP_IADD_S16:
        case OP_IMUL_S16:
        case OP_IOFFSET_S16:
        case OP_IOFFSET_S16_LOAD:
        case OP_IOFFSET_S16_STORE:
        case OP_ARRAY_U16:
        case OP_ARRAY_U16_LOAD:
        case OP_ARRAY_U16_STORE:
        case OP_LOCAL_U16:
        case OP_LOCAL_U16_LOAD:
        case OP_LOCAL_U16_STORE:
        case OP_STATIC_U16:
        case OP_STATIC_U16_LOAD:
        case OP_STATIC_U16_STORE:
        case OP_GLOBAL_U16:
        case OP_GLOBAL_U16_LOAD:
        case OP_GLOBAL_U16_STORE:
        case OP_J:
        case OP_JZ:
        case OP_IEQ_JZ:
        case OP_INE_JZ:
        case OP_IGT_JZ:
        case OP_IGE_JZ:
        case OP_ILT_JZ:
        case OP_ILE_JZ:
            size = 3;
            break;
        case OP_PUSH_CONST_U8_U8_U8:
        case OP_NATIVE:
        case OP_CALL:
        case OP_STATIC_U24:
        case OP_STATIC_U24_LOAD:
        case OP_STATIC_U24_STORE:
        case OP_GLOBAL_U24:
        case OP_GLOBAL_U24_LOAD:
        case OP_GLOBAL_U24_STORE:
        case OP_PUSH_CONST_U24:
            size = 4;
            break;
        case OP_PUSH_CONST_U32:
        case OP_PUSH_CONST_F:
            size = 5;
            break;
        case OP_SWITCH:
            if (view->Read(data, cursor, 2) < 2)
                return 0;
            size = 2 + static_cast<size_t>(data[1]) * sizeof(SwitchCase);
            break;
        case OP_IS_BIT_SET:
            size = 1;
            break;
        case OP_ENTER:
            if (view->Read(data, cursor, 5) < 5)
                return 0;
            size = 5 + data[4];
            break;
        default:
            size = 1;
            break;
        }
        cursor += size;
    }
    return 0;
}

uint32_t LocalTemp(uint32_t index)
{
    return LLIL_TEMP(YSC_LOCAL_TEMP_BASE + index);
}

uint32_t StackTemp(uint32_t blockIndex, uint32_t index)
{
    return LLIL_TEMP(YSC_STACK_TEMP_BASE + blockIndex * 256 + index);
}

uint32_t CallResultTemp(uint64_t address)
{
    return LLIL_TEMP(YSC_CALL_RESULT_TEMP_BASE + static_cast<uint32_t>(address & 0xffff));
}

uint32_t ArgReg(uint32_t index)
{
    return index < 16 ? Reg_ARG0 + index : Reg_ARG15;
}

uint32_t ReturnReg(uint32_t index)
{
    return index < 4 ? Reg_R1 + index : Reg_R4;
}

BinaryNinja::Ref<BinaryNinja::Type> VolatileInt32Type()
{
    BinaryNinja::TypeBuilder builder(BinaryNinja::Type::IntegerType(4, true));
    builder.SetVolatile(BinaryNinja::Confidence<bool>(true));
    return builder.Finalize();
}

void ApplyYSCFunctionType(BinaryNinja::Function* function, BinaryNinja::BinaryView* view,
                          const std::optional<YSCEnterInfo>& enter, const std::optional<uint8_t>& analyzedReturnCount)
{
    if (!function || !view || !enter)
        return;
    if (enter->m_paramCount > 16)
        return;

    using namespace BinaryNinja;
    std::vector<FunctionParameter> params;
    std::vector<Variable> paramVars;
    params.reserve(enter->m_paramCount);
    paramVars.reserve(enter->m_paramCount);
    for (uint8_t i = 0; i < enter->m_paramCount; i++)
    {
        auto paramType = Type::IntegerType(4, true);
        Variable paramVar(RegisterVariableSourceType, ArgReg(i));
        params.emplace_back(fmt::format("arg{}", i + 1), paramType, false, paramVar);
        paramVars.push_back(paramVar);
        function->CreateAutoVariable(paramVar, paramType, fmt::format("arg{}", i + 1), true);
    }

    uint8_t retCount = analyzedReturnCount.value_or(FindFirstLeaveReturnCount(view, function->GetStart()));
    Ref<Type> returnType = retCount > 0 ? Type::IntegerType(4, true) : Type::VoidType();
    auto cc = function->GetArchitecture()->GetDefaultCallingConvention();
    function->SetAutoType(Type::FunctionType(returnType, cc, params, false, 0));
    function->SetAutoParameterVariables(Confidence<std::vector<Variable>>(paramVars, 255));
}

size_t GetRawInstructionLength(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* data, size_t len);
bool IsZeroReturnLeaveBlock(BinaryNinja::BinaryView* view, const BinaryNinja::Ref<BinaryNinja::BasicBlock>& block,
                            size_t maxInstructionLength);

bool GetYSCStackEffect(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* opcode, size_t len, int& delta,
                       bool& terminal)
{
    if (!opcode || len < 1 || opcode[0] >= OP_MAX)
        return false;

    delta = 0;
    terminal = false;

    switch (opcode[0])
    {
    case OP_NOP:
    case OP_ENTER:
        return true;
    case OP_PUSH_CONST_M1:
    case OP_PUSH_CONST_0:
    case OP_PUSH_CONST_1:
    case OP_PUSH_CONST_2:
    case OP_PUSH_CONST_3:
    case OP_PUSH_CONST_4:
    case OP_PUSH_CONST_5:
    case OP_PUSH_CONST_6:
    case OP_PUSH_CONST_7:
    case OP_PUSH_CONST_FM1:
    case OP_PUSH_CONST_F0:
    case OP_PUSH_CONST_F1:
    case OP_PUSH_CONST_F2:
    case OP_PUSH_CONST_F3:
    case OP_PUSH_CONST_F4:
    case OP_PUSH_CONST_F5:
    case OP_PUSH_CONST_F6:
    case OP_PUSH_CONST_F7:
    case OP_PUSH_CONST_U8:
    case OP_PUSH_CONST_S16:
    case OP_PUSH_CONST_U24:
    case OP_PUSH_CONST_U32:
    case OP_PUSH_CONST_F:
    case OP_LOCAL_U8:
    case OP_LOCAL_U16:
    case OP_LOCAL_U8_LOAD:
    case OP_LOCAL_U16_LOAD:
    case OP_STATIC_U8:
    case OP_STATIC_U16:
    case OP_STATIC_U24:
    case OP_STATIC_U8_LOAD:
    case OP_STATIC_U16_LOAD:
    case OP_STATIC_U24_LOAD:
    case OP_GLOBAL_U16:
    case OP_GLOBAL_U24:
    case OP_GLOBAL_U16_LOAD:
    case OP_GLOBAL_U24_LOAD:
        delta = 1;
        return true;
    case OP_PUSH_CONST_U8_U8:
        delta = 2;
        return true;
    case OP_PUSH_CONST_U8_U8_U8:
        delta = 3;
        return true;
    case OP_DUP:
        delta = 1;
        return true;
    case OP_DROP:
    case OP_ARRAY_U8:
    case OP_ARRAY_U16:
    case OP_ARRAY_U8_LOAD:
    case OP_ARRAY_U16_LOAD:
    case OP_STATIC_U8_STORE:
    case OP_STATIC_U16_STORE:
    case OP_STATIC_U24_STORE:
    case OP_GLOBAL_U16_STORE:
    case OP_GLOBAL_U24_STORE:
    case OP_JZ:
    case OP_SWITCH:
        delta = -1;
        return true;
    case OP_IADD:
    case OP_ISUB:
    case OP_IMUL:
    case OP_IDIV:
    case OP_IMOD:
    case OP_IAND:
    case OP_IOR:
    case OP_IXOR:
    case OP_IEQ:
    case OP_INE:
    case OP_IGT:
    case OP_IGE:
    case OP_ILT:
    case OP_ILE:
    case OP_FADD:
    case OP_FSUB:
    case OP_FMUL:
    case OP_FDIV:
    case OP_FEQ:
    case OP_FNE:
    case OP_FGT:
    case OP_FGE:
    case OP_FLT:
    case OP_FLE:
    case OP_IOFFSET:
        delta = -1;
        return true;
    case OP_INEG:
    case OP_INOT:
    case OP_FNEG:
    case OP_I2F:
    case OP_F2I:
    case OP_IADD_U8:
    case OP_IMUL_U8:
    case OP_IADD_S16:
    case OP_IMUL_S16:
    case OP_IOFFSET_U8:
    case OP_IOFFSET_S16:
    case OP_IOFFSET_U8_LOAD:
    case OP_IOFFSET_S16_LOAD:
    case OP_LOAD:
    case OP_STRING:
    case OP_STRINGHASH:
        return true;
    case OP_IOFFSET_U8_STORE:
    case OP_IOFFSET_S16_STORE:
    case OP_STORE:
    case OP_IEQ_JZ:
    case OP_INE_JZ:
    case OP_IGT_JZ:
    case OP_IGE_JZ:
    case OP_ILT_JZ:
    case OP_ILE_JZ:
        delta = -2;
        return true;
    case OP_STORE_REV:
        delta = -1;
        return true;
    case OP_ARRAY_U8_STORE:
    case OP_ARRAY_U16_STORE:
        delta = -3;
        return true;
    case OP_IS_BIT_SET:
        delta = -1;
        return true;
    case OP_LOCAL_U8_STORE:
    case OP_LOCAL_U16_STORE:
        delta = -1;
        return true;
    case OP_TEXT_LABEL_ASSIGN_STRING:
    case OP_TEXT_LABEL_ASSIGN_INT:
    case OP_TEXT_LABEL_APPEND_STRING:
    case OP_TEXT_LABEL_APPEND_INT:
        delta = -2;
        return true;
    case OP_TEXT_LABEL_COPY:
        delta = -3;
        return true;
    case OP_J:
        terminal = true;
        return true;
    case OP_LEAVE:
        if (len < 3)
            return false;
        delta = -static_cast<int>(opcode[2]);
        terminal = true;
        return true;
    case OP_NATIVE:
        if (len < 4)
            return false;
        delta = static_cast<int>(opcode[1] & 3) - static_cast<int>((opcode[1] >> 2) & 0x3f);
        return true;
    case OP_CALL:
    {
        if (len < 4 || !view)
            return false;
        auto code = view->GetSectionByName("CODE");
        if (!code)
            return false;
        uint64_t target = code->GetStart() + DecodeU24(opcode + 1);
        delta = static_cast<int>(FindFirstLeaveReturnCount(view, target)) - static_cast<int>(GetEnterParamCount(view, target));
        return true;
    }
    default:
        return false;
    }
}

bool PopAnalysisValue(std::vector<std::optional<uint32_t>>& stack, std::optional<uint32_t>* value = nullptr)
{
    if (stack.empty())
        return false;
    if (value)
        *value = stack.back();
    stack.pop_back();
    return true;
}

bool ApplyYSCStackAnalysisEffect(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* opcode, size_t len,
                                 std::vector<std::optional<uint32_t>>& stack, bool& terminal,
                                 YSCLiftDiagnostic* diagnostic)
{
    if (!opcode || len < 1)
        return false;

    auto failUnderflow = [&]() {
        if (diagnostic)
        {
            diagnostic->reason = "stack-depth-out-of-range";
            diagnostic->address = addr;
            diagnostic->opcode = opcode[0];
            diagnostic->depth = -1;
        }
        return false;
    };

    auto pushUnknown = [&]() { stack.emplace_back(std::nullopt); };
    auto pushConst = [&](uint32_t value) { stack.emplace_back(value); };

    switch (opcode[0])
    {
    case OP_PUSH_CONST_M1: pushConst(static_cast<uint32_t>(-1)); terminal = false; return true;
    case OP_PUSH_CONST_0: case OP_PUSH_CONST_1: case OP_PUSH_CONST_2: case OP_PUSH_CONST_3:
    case OP_PUSH_CONST_4: case OP_PUSH_CONST_5: case OP_PUSH_CONST_6: case OP_PUSH_CONST_7:
        pushConst(opcode[0] - OP_PUSH_CONST_0); terminal = false; return true;
    case OP_PUSH_CONST_U8:
        if (len < 2) return false;
        pushConst(opcode[1]); terminal = false; return true;
    case OP_PUSH_CONST_U8_U8:
        if (len < 3) return false;
        pushConst(opcode[1]); pushConst(opcode[2]); terminal = false; return true;
    case OP_PUSH_CONST_U8_U8_U8:
        if (len < 4) return false;
        pushConst(opcode[1]); pushConst(opcode[2]); pushConst(opcode[3]); terminal = false; return true;
    case OP_PUSH_CONST_S16:
        if (len < 3) return false;
        pushConst(static_cast<uint32_t>(ReadUnaligned<int16_t>(opcode + 1))); terminal = false; return true;
    case OP_PUSH_CONST_U24:
        if (len < 4) return false;
        pushConst(DecodeU24(opcode + 1)); terminal = false; return true;
    case OP_PUSH_CONST_U32:
        if (len < 5) return false;
        pushConst(ReadUnaligned<uint32_t>(opcode + 1)); terminal = false; return true;
    case OP_DUP:
        if (stack.empty()) return failUnderflow();
        stack.push_back(stack.back()); terminal = false; return true;
    case OP_DROP:
        if (!stack.empty())
            stack.pop_back();
        terminal = false;
        return true;
    case OP_STORE_REV:
        if (stack.size() < 2)
            return failUnderflow();
        stack.pop_back();
        terminal = false;
        return true;
    case OP_LOAD_N:
    case OP_STORE_N:
    {
        std::optional<uint32_t> address;
        std::optional<uint32_t> count;
        if (!PopAnalysisValue(stack, &address) || !PopAnalysisValue(stack, &count))
            return failUnderflow();
        if (!count || *count > 64)
        {
            if (diagnostic)
            {
                diagnostic->reason = count ? "load-store-n-count-too-large" : "load-store-n-count-unknown";
                diagnostic->address = addr;
                diagnostic->opcode = opcode[0];
                diagnostic->depth = static_cast<int>(stack.size());
                diagnostic->otherDepth = count ? static_cast<int>(*count) : -1;
            }
            return false;
        }
        if (opcode[0] == OP_LOAD_N)
        {
            for (uint32_t i = 0; i < *count; i++)
                pushUnknown();
        }
        else
        {
            for (uint32_t i = 0; i < *count; i++)
            {
                if (!PopAnalysisValue(stack))
                    return failUnderflow();
            }
        }
        terminal = false;
        return true;
    }
    default:
        break;
    }

    int delta = 0;
    bool instrTerminal = false;
    if (!GetYSCStackEffect(view, addr, opcode, len, delta, instrTerminal))
    {
        if (diagnostic)
        {
            diagnostic->reason = "unknown-stack-effect";
            diagnostic->address = addr;
            diagnostic->opcode = opcode[0];
            diagnostic->depth = static_cast<int>(stack.size());
        }
        return false;
    }
    if (delta < 0)
    {
        for (int i = 0; i < -delta; i++)
            if (!PopAnalysisValue(stack))
                return failUnderflow();
    }
    else
    {
        for (int i = 0; i < delta; i++)
            pushUnknown();
    }
    if (stack.size() > 256)
    {
        if (diagnostic)
        {
            diagnostic->reason = "stack-depth-out-of-range";
            diagnostic->address = addr;
            diagnostic->opcode = opcode[0];
            diagnostic->depth = static_cast<int>(stack.size());
        }
        return false;
    }
    terminal = instrTerminal;
    return true;
}

bool MergeAnalysisStackStates(std::vector<std::optional<uint32_t>>& existing,
                              const std::vector<std::optional<uint32_t>>& incoming)
{
    bool changed = false;
    if (existing.size() != incoming.size())
    {
        size_t mergedSize = std::max(existing.size(), incoming.size());
        existing.assign(mergedSize, std::nullopt);
        return true;
    }
    for (size_t i = 0; i < existing.size(); i++)
    {
        if (existing[i] != incoming[i] && existing[i].has_value())
        {
            existing[i].reset();
            changed = true;
        }
    }
    return changed;
}

bool AnalyzeFunctionVMStack(BinaryNinja::BinaryView* view, const std::vector<BinaryNinja::Ref<BinaryNinja::BasicBlock>>& blocks,
                            size_t maxInstructionLength, std::map<uint64_t, size_t>& inputDepths, uint64_t entryAddress,
                            YSCLiftDiagnostic* diagnostic)
{
    if (!view || blocks.empty())
    {
        if (diagnostic) diagnostic->reason = "no-view-or-blocks";
        return false;
    }

    std::map<uint64_t, BinaryNinja::Ref<BinaryNinja::BasicBlock>> blockByStart;
    for (auto& block : blocks)
        blockByStart[block->GetStart()] = block;

    if (!blockByStart.contains(entryAddress))
        entryAddress = blocks.front()->GetStart();

    std::queue<uint64_t> work;
    std::map<uint64_t, std::vector<std::optional<uint32_t>>> inputStates;
    inputDepths[entryAddress] = 0;
    inputStates[entryAddress] = {};
    work.push(entryAddress);
    size_t workIterations = 0;

    while (!work.empty())
    {
        if (++workIterations > YSC_STACK_ANALYSIS_WORK_LIMIT)
        {
            if (diagnostic) { diagnostic->reason = "work-limit"; diagnostic->address = work.front(); diagnostic->otherDepth = static_cast<int>(work.size()); }
            return false;
        }
        uint64_t blockStart = work.front();
        work.pop();

        auto blockIt = blockByStart.find(blockStart);
        if (blockIt == blockByStart.end())
        {
            if (diagnostic) { diagnostic->reason = "missing-block"; diagnostic->address = blockStart; }
            return false;
        }

        auto block = blockIt->second;
        auto stateIt = inputStates.find(blockStart);
        if (stateIt == inputStates.end())
        {
            if (diagnostic) { diagnostic->reason = "missing-stack-state"; diagnostic->address = blockStart; }
            return false;
        }
        std::vector<std::optional<uint32_t>> stack = stateIt->second;
        bool terminal = false;
        bool terminalIsExplicitJump = false;

        for (uint64_t addr = block->GetStart(); addr < block->GetEnd();)
        {
            std::vector<uint8_t> opcode(maxInstructionLength);
            size_t len = view->Read(opcode.data(), addr, opcode.size());
            if (len == 0)
            {
                if (diagnostic) { diagnostic->reason = "read-failed"; diagnostic->address = addr; }
                return false;
            }
            size_t instrLen = GetRawInstructionLength(view, addr, opcode.data(), len);
            if (instrLen == 0 || addr + instrLen <= addr)
            {
                if (diagnostic) { diagnostic->reason = "bad-instruction-length"; diagnostic->address = addr; diagnostic->opcode = opcode[0]; }
                return false;
            }

            bool instrTerminal = false;
            if (!ApplyYSCStackAnalysisEffect(view, addr, opcode.data(), instrLen, stack, instrTerminal, diagnostic))
                return false;
            terminal = instrTerminal;
            terminalIsExplicitJump = instrTerminal && opcode[0] == OP_J;
            addr += instrLen;
        }

        if (terminal && !terminalIsExplicitJump)
            continue;

        for (auto& edge : block->GetOutgoingEdges())
        {
            if (!edge.target)
            {
                if (diagnostic) { diagnostic->reason = "null-edge-target"; diagnostic->address = block->GetEnd(); diagnostic->depth = static_cast<int>(stack.size()); }
                return false;
            }
            uint64_t target = edge.target->GetStart();
            auto existingState = inputStates.find(target);
            if (existingState == inputStates.end())
            {
                inputStates[target] = stack;
                inputDepths[target] = stack.size();
                work.push(target);
            }
            else
            {
                if (existingState->second.size() != stack.size() && IsZeroReturnLeaveBlock(view, edge.target, maxInstructionLength))
                    continue;

                bool changed = MergeAnalysisStackStates(existingState->second, stack);
                if (existingState->second.size() > 256)
                {
                    if (diagnostic) { diagnostic->reason = "merged-depth-out-of-range"; diagnostic->address = block->GetStart(); diagnostic->target = target; diagnostic->depth = static_cast<int>(stack.size()); diagnostic->otherDepth = static_cast<int>(inputDepths[target]); }
                    return false;
                }
                if (changed)
                {
                    inputDepths[target] = existingState->second.size();
                    work.push(target);
                }
            }
        }
    }

    for (auto& block : blocks)
    {
        if (!inputDepths.contains(block->GetStart()))
        {
            if (diagnostic) { diagnostic->reason = "unreached-block"; diagnostic->address = block->GetStart(); }
            return false;
        }
    }

    return true;
}

bool AnalyzeFunctionVMStack(BinaryNinja::BinaryView* view, const std::vector<BinaryNinja::Ref<BinaryNinja::BasicBlock>>& blocks,
                            size_t maxInstructionLength, std::map<uint64_t, size_t>& inputDepths)
{
    return AnalyzeFunctionVMStack(view, blocks, maxInstructionLength, inputDepths,
                                  blocks.empty() ? 0 : blocks.front()->GetStart(), nullptr);
}

bool IsZeroReturnLeaveBlock(BinaryNinja::BinaryView* view, const BinaryNinja::Ref<BinaryNinja::BasicBlock>& block,
                            size_t maxInstructionLength)
{
    if (!view || !block)
        return false;

    std::vector<uint8_t> opcode(maxInstructionLength);
    size_t len = view->Read(opcode.data(), block->GetStart(), opcode.size());
    if (len < 3)
        return false;
    return opcode[0] == OP_LEAVE && opcode[2] == 0;
}

bool IsSafeWholeFunctionLiftOpcode(uint8_t opcode)
{
    switch (opcode)
    {
    case OP_NOP:
    case OP_ENTER:
    case OP_PUSH_CONST_M1:
    case OP_PUSH_CONST_0:
    case OP_PUSH_CONST_1:
    case OP_PUSH_CONST_2:
    case OP_PUSH_CONST_3:
    case OP_PUSH_CONST_4:
    case OP_PUSH_CONST_5:
    case OP_PUSH_CONST_6:
    case OP_PUSH_CONST_7:
    case OP_PUSH_CONST_FM1:
    case OP_PUSH_CONST_F0:
    case OP_PUSH_CONST_F1:
    case OP_PUSH_CONST_F2:
    case OP_PUSH_CONST_F3:
    case OP_PUSH_CONST_F4:
    case OP_PUSH_CONST_F5:
    case OP_PUSH_CONST_F6:
    case OP_PUSH_CONST_F7:
    case OP_PUSH_CONST_U8:
    case OP_PUSH_CONST_S16:
    case OP_PUSH_CONST_U24:
    case OP_PUSH_CONST_U32:
    case OP_PUSH_CONST_F:
    case OP_DUP:
    case OP_DROP:
    case OP_IADD:
    case OP_ISUB:
    case OP_IMUL:
    case OP_IDIV:
    case OP_IMOD:
    case OP_IAND:
    case OP_IOR:
    case OP_IXOR:
    case OP_INEG:
    case OP_INOT:
    case OP_IEQ:
    case OP_INE:
    case OP_IGT:
    case OP_IGE:
    case OP_ILT:
    case OP_ILE:
    case OP_FADD:
    case OP_FSUB:
    case OP_FMUL:
    case OP_FDIV:
    case OP_FNEG:
    case OP_FEQ:
    case OP_FNE:
    case OP_FGT:
    case OP_FGE:
    case OP_FLT:
    case OP_FLE:
    case OP_I2F:
    case OP_F2I:
    case OP_IADD_U8:
    case OP_IMUL_U8:
    case OP_IADD_S16:
    case OP_IMUL_S16:
    case OP_IOFFSET:
    case OP_IOFFSET_U8:
    case OP_IOFFSET_S16:
    case OP_IOFFSET_U8_LOAD:
    case OP_IOFFSET_S16_LOAD:
    case OP_IOFFSET_U8_STORE:
    case OP_IOFFSET_S16_STORE:
    case OP_LOCAL_U8:
    case OP_LOCAL_U16:
    case OP_LOCAL_U8_LOAD:
    case OP_LOCAL_U16_LOAD:
    case OP_LOCAL_U8_STORE:
    case OP_LOCAL_U16_STORE:
    case OP_STATIC_U8:
    case OP_STATIC_U16:
    case OP_STATIC_U24:
    case OP_STATIC_U8_LOAD:
    case OP_STATIC_U16_LOAD:
    case OP_STATIC_U24_LOAD:
    case OP_STATIC_U8_STORE:
    case OP_STATIC_U16_STORE:
    case OP_STATIC_U24_STORE:
    case OP_GLOBAL_U16:
    case OP_GLOBAL_U24:
    case OP_GLOBAL_U16_LOAD:
    case OP_GLOBAL_U24_LOAD:
    case OP_GLOBAL_U16_STORE:
    case OP_GLOBAL_U24_STORE:
    case OP_LOAD:
    case OP_STORE:
    case OP_STORE_REV:
    case OP_LOAD_N:
    case OP_STORE_N:
    case OP_ARRAY_U8:
    case OP_ARRAY_U8_LOAD:
    case OP_ARRAY_U8_STORE:
    case OP_ARRAY_U16:
    case OP_ARRAY_U16_LOAD:
    case OP_ARRAY_U16_STORE:
    case OP_STRING:
    case OP_STRINGHASH:
    case OP_NATIVE:
    case OP_CALL:
    case OP_IS_BIT_SET:
    case OP_J:
    case OP_JZ:
    case OP_IEQ_JZ:
    case OP_INE_JZ:
    case OP_IGT_JZ:
    case OP_IGE_JZ:
    case OP_ILT_JZ:
    case OP_ILE_JZ:
    case OP_SWITCH:
    case OP_LEAVE:
    case OP_TEXT_LABEL_ASSIGN_STRING:
    case OP_TEXT_LABEL_ASSIGN_INT:
    case OP_TEXT_LABEL_APPEND_STRING:
    case OP_TEXT_LABEL_APPEND_INT:
    case OP_TEXT_LABEL_COPY:
        return true;
    default:
        return false;
    }
}

bool CanUseWholeFunctionStackLifting(BinaryNinja::BinaryView* view,
                                     const std::vector<BinaryNinja::Ref<BinaryNinja::BasicBlock>>& blocks,
                                     size_t maxInstructionLength, YSCLiftDiagnostic* diagnostic)
{
    if (!view || blocks.empty())
    {
        if (diagnostic) diagnostic->reason = "no-view-or-blocks";
        return false;
    }

    uint64_t totalSize = 0;
    for (auto& block : blocks)
    {
        totalSize += block->GetLength();
        if (totalSize > YSC_WHOLE_FUNCTION_LIFT_SIZE_LIMIT)
        {
            if (diagnostic)
            {
                diagnostic->reason = "size-limit";
                diagnostic->address = block->GetStart();
                diagnostic->totalSize = totalSize;
            }
            return false;
        }

        for (uint64_t addr = block->GetStart(); addr < block->GetEnd();)
        {
            std::vector<uint8_t> opcode(maxInstructionLength);
            size_t len = view->Read(opcode.data(), addr, opcode.size());
            if (len == 0)
            {
                if (diagnostic) { diagnostic->reason = "read-failed"; diagnostic->address = addr; }
                return false;
            }
            size_t instrLen = GetRawInstructionLength(view, addr, opcode.data(), len);
            if (instrLen == 0 || addr + instrLen <= addr)
            {
                if (diagnostic) { diagnostic->reason = "bad-instruction-length"; diagnostic->address = addr; diagnostic->opcode = opcode[0]; }
                return false;
            }
            if (!IsSafeWholeFunctionLiftOpcode(opcode[0]))
            {
                if (diagnostic) { diagnostic->reason = "unsafe-opcode"; diagnostic->address = addr; diagnostic->opcode = opcode[0]; }
                return false;
            }
            addr += instrLen;
        }
    }
    return true;
}

bool CanUseWholeFunctionStackLifting(BinaryNinja::BinaryView* view,
                                     const std::vector<BinaryNinja::Ref<BinaryNinja::BasicBlock>>& blocks,
                                     size_t maxInstructionLength)
{
    return CanUseWholeFunctionStackLifting(view, blocks, maxInstructionLength, nullptr);
}

bool IsControlFlowOpcode(uint8_t opcode)
{
    switch (opcode)
    {
    case OP_J:
    case OP_JZ:
    case OP_IEQ_JZ:
    case OP_INE_JZ:
    case OP_IGT_JZ:
    case OP_IGE_JZ:
    case OP_ILT_JZ:
    case OP_ILE_JZ:
    case OP_SWITCH:
    case OP_LEAVE:
    case OP_THROW:
        return true;
    default:
        return false;
    }
}

size_t GetRawInstructionLength(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* data, size_t len)
{
    if (!data || len < 1)
        return 0;

    switch (data[0])
    {
    case OP_PUSH_CONST_U8:
    case OP_ARRAY_U8:
    case OP_ARRAY_U8_LOAD:
    case OP_ARRAY_U8_STORE:
    case OP_LOCAL_U8:
    case OP_LOCAL_U8_LOAD:
    case OP_LOCAL_U8_STORE:
    case OP_STATIC_U8:
    case OP_STATIC_U8_LOAD:
    case OP_STATIC_U8_STORE:
    case OP_IADD_U8:
    case OP_IMUL_U8:
    case OP_IOFFSET_U8:
    case OP_IOFFSET_U8_LOAD:
    case OP_IOFFSET_U8_STORE:
    case OP_TEXT_LABEL_ASSIGN_STRING:
    case OP_TEXT_LABEL_ASSIGN_INT:
    case OP_TEXT_LABEL_APPEND_STRING:
    case OP_TEXT_LABEL_APPEND_INT:
    case OP_TEXT_LABEL_COPY:
        return 2;
    case OP_PUSH_CONST_U8_U8:
    case OP_PUSH_CONST_S16:
    case OP_IADD_S16:
    case OP_IMUL_S16:
    case OP_IOFFSET_S16:
    case OP_IOFFSET_S16_LOAD:
    case OP_IOFFSET_S16_STORE:
    case OP_ARRAY_U16:
    case OP_ARRAY_U16_LOAD:
    case OP_ARRAY_U16_STORE:
    case OP_LOCAL_U16:
    case OP_LOCAL_U16_LOAD:
    case OP_LOCAL_U16_STORE:
    case OP_STATIC_U16:
    case OP_STATIC_U16_LOAD:
    case OP_STATIC_U16_STORE:
    case OP_GLOBAL_U16:
    case OP_GLOBAL_U16_LOAD:
    case OP_GLOBAL_U16_STORE:
    case OP_J:
    case OP_JZ:
    case OP_IEQ_JZ:
    case OP_INE_JZ:
    case OP_IGT_JZ:
    case OP_IGE_JZ:
    case OP_ILT_JZ:
    case OP_ILE_JZ:
    case OP_LEAVE:
        return 3;
    case OP_PUSH_CONST_U8_U8_U8:
    case OP_NATIVE:
    case OP_CALL:
    case OP_STATIC_U24:
    case OP_STATIC_U24_LOAD:
    case OP_STATIC_U24_STORE:
    case OP_GLOBAL_U24:
    case OP_GLOBAL_U24_LOAD:
    case OP_GLOBAL_U24_STORE:
    case OP_PUSH_CONST_U24:
        return 4;
    case OP_PUSH_CONST_U32:
    case OP_PUSH_CONST_F:
        return 5;
    case OP_ENTER:
    {
        if (len < 5)
            return 0;
        size_t size = 5 + data[4];
        if (len < size)
            return 0;
        return size;
    }
    case OP_SWITCH:
    {
        if (len < 2)
            return 0;
        size_t size = 2 + static_cast<size_t>(data[1]) * sizeof(SwitchCase);
        if (len < size)
            return 0;
        return size;
    }
    default:
        if (data[0] >= OP_MAX)
            return 0;
        return 1;
    }
}

bool BlockContainsControlFlow(BinaryNinja::BinaryView* view, BinaryNinja::BasicBlock* block, size_t maxInstructionLength)
{
    if (!view || !block)
        return true;

    for (uint64_t addr = block->GetStart(); addr < block->GetEnd();)
    {
        std::vector<uint8_t> ownedOpcode;
        ownedOpcode.resize(maxInstructionLength);
        size_t len = view->Read(ownedOpcode.data(), addr, ownedOpcode.size());
        if (len == 0)
            return true;

        if (IsControlFlowOpcode(ownedOpcode[0]))
            return true;

        size_t instrLen = GetRawInstructionLength(view, addr, ownedOpcode.data(), len);
        if (instrLen == 0 || addr + instrLen <= addr)
            return true;

        addr += instrLen;
    }
    return false;
}

size_t CountRawInstructions(BinaryNinja::BinaryView* view, BinaryNinja::BasicBlock* block, size_t maxInstructionLength)
{
    if (!view || !block)
        return 0;

    size_t count = 0;
    for (uint64_t addr = block->GetStart(); addr < block->GetEnd();)
    {
        std::vector<uint8_t> ownedOpcode(maxInstructionLength);
        size_t len = view->Read(ownedOpcode.data(), addr, ownedOpcode.size());
        if (len == 0)
            return count;

        size_t instrLen = GetRawInstructionLength(view, addr, ownedOpcode.data(), len);
        if (instrLen == 0 || addr + instrLen <= addr)
            return count;

        count++;
        addr += instrLen;
    }
    return count;
}

class YSCSymbolicLifter
{
  public:
    YSCSymbolicLifter(YSCArchitecture* arch, BinaryNinja::LowLevelILFunction& il, BinaryNinja::BinaryView* view) :
        m_arch(arch), m_il(il), m_view(view)
    {}

    bool Lift(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (!m_symbolic)
            return false;
        if (len < 1 || opcode[0] >= OP_MAX)
            return false;

        m_il.SetCurrentAddress(m_arch, addr);
        const uint8_t* data = opcode + 1;
        switch (opcode[0])
        {
        case OP_NOP:
            m_il.AddInstruction(m_il.Nop());
            len = 1;
            return true;
        case OP_ENTER:
            return Enter(opcode, len);
        case OP_PUSH_CONST_M1: Push(m_il.Const(4, static_cast<uint32_t>(-1)), static_cast<uint32_t>(-1)); len = 1; return true;
        case OP_PUSH_CONST_0: case OP_PUSH_CONST_1: case OP_PUSH_CONST_2: case OP_PUSH_CONST_3:
        case OP_PUSH_CONST_4: case OP_PUSH_CONST_5: case OP_PUSH_CONST_6: case OP_PUSH_CONST_7:
            Push(m_il.Const(4, opcode[0] - OP_PUSH_CONST_0), opcode[0] - OP_PUSH_CONST_0); len = 1; return true;
        case OP_PUSH_CONST_FM1: Push(m_il.FloatConstSingle(-1.0f)); len = 1; return true;
        case OP_PUSH_CONST_F0: case OP_PUSH_CONST_F1: case OP_PUSH_CONST_F2: case OP_PUSH_CONST_F3:
        case OP_PUSH_CONST_F4: case OP_PUSH_CONST_F5: case OP_PUSH_CONST_F6: case OP_PUSH_CONST_F7:
            Push(m_il.FloatConstSingle(static_cast<float>(opcode[0] - OP_PUSH_CONST_F0))); len = 1; return true;
        case OP_PUSH_CONST_U8: if (len < 2) return false; Push(m_il.Const(4, data[0]), data[0]); len = 2; return true;
        case OP_PUSH_CONST_U8_U8: if (len < 3) return false; Push(m_il.Const(4, data[0]), data[0]); Push(m_il.Const(4, data[1]), data[1]); len = 3; return true;
        case OP_PUSH_CONST_U8_U8_U8: if (len < 4) return false; Push(m_il.Const(4, data[0]), data[0]); Push(m_il.Const(4, data[1]), data[1]); Push(m_il.Const(4, data[2]), data[2]); len = 4; return true;
        case OP_PUSH_CONST_S16: if (len < 3) return false; { uint32_t value = static_cast<uint32_t>(ReadUnaligned<int16_t>(data)); Push(m_il.Const(4, value), value); } len = 3; return true;
        case OP_PUSH_CONST_U24: if (len < 4) return false; { uint32_t value = DecodeU24(data); Push(m_il.Const(4, value), value); } len = 4; return true;
        case OP_PUSH_CONST_U32: if (len < 5) return false; { uint32_t value = ReadUnaligned<uint32_t>(data); Push(m_il.Const(4, value), value); } len = 5; return true;
        case OP_PUSH_CONST_F: if (len < 5) return false; Push(m_il.FloatConstSingle(ReadUnaligned<float>(data))); len = 5; return true;
        case OP_DUP: return Dup(len);
        case OP_DROP: return Drop(len);
        case OP_IADD: return Binary(len, &BinaryNinja::LowLevelILFunction::Add);
        case OP_ISUB: return Binary(len, &BinaryNinja::LowLevelILFunction::Sub);
        case OP_IMUL: return Binary(len, &BinaryNinja::LowLevelILFunction::Mult);
        case OP_IDIV: return Binary(len, &BinaryNinja::LowLevelILFunction::DivSigned);
        case OP_IMOD: return Binary(len, &BinaryNinja::LowLevelILFunction::ModSigned);
        case OP_IAND: return Binary(len, &BinaryNinja::LowLevelILFunction::And);
        case OP_IOR: return Binary(len, &BinaryNinja::LowLevelILFunction::Or);
        case OP_IXOR: return Binary(len, &BinaryNinja::LowLevelILFunction::Xor);
        case OP_INEG: return Unary(len, &BinaryNinja::LowLevelILFunction::Neg);
        case OP_INOT: return UnaryCustom(len, [&](BinaryNinja::ExprId v) { return m_il.CompareEqual(4, v, m_il.Const(4, 0)); });
        case OP_IEQ: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareEqual);
        case OP_INE: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareNotEqual);
        case OP_IGT: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareSignedGreaterThan);
        case OP_IGE: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareSignedGreaterEqual);
        case OP_ILT: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareSignedLessThan);
        case OP_ILE: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareSignedLessEqual);
        case OP_FADD: return Binary(len, &BinaryNinja::LowLevelILFunction::FloatAdd);
        case OP_FSUB: return Binary(len, &BinaryNinja::LowLevelILFunction::FloatSub);
        case OP_FMUL: return Binary(len, &BinaryNinja::LowLevelILFunction::FloatMult);
        case OP_FDIV: return Binary(len, &BinaryNinja::LowLevelILFunction::FloatDiv);
        case OP_FNEG: return Unary(len, &BinaryNinja::LowLevelILFunction::FloatNeg);
        case OP_FEQ: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareEqual);
        case OP_FNE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareNotEqual);
        case OP_FGT: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareGreaterThan);
        case OP_FGE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareGreaterEqual);
        case OP_FLT: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareLessThan);
        case OP_FLE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareLessEqual);
        case OP_I2F: return Unary(len, &BinaryNinja::LowLevelILFunction::IntToFloat);
        case OP_F2I: return Unary(len, &BinaryNinja::LowLevelILFunction::FloatToInt);
        case OP_IADD_U8: if (len < 2) return false; return ImmBinary(len, data[0], &BinaryNinja::LowLevelILFunction::Add);
        case OP_IMUL_U8: if (len < 2) return false; return ImmBinary(len, data[0], &BinaryNinja::LowLevelILFunction::Mult);
        case OP_IADD_S16: if (len < 3) return false; return ImmBinary(len, static_cast<uint32_t>(ReadUnaligned<int16_t>(data)), &BinaryNinja::LowLevelILFunction::Add, 3);
        case OP_IMUL_S16: if (len < 3) return false; return ImmBinary(len, static_cast<uint32_t>(ReadUnaligned<int16_t>(data)), &BinaryNinja::LowLevelILFunction::Mult, 3);
        case OP_IOFFSET: return IOffset(len);
        case OP_IOFFSET_U8: if (len < 2) return false; return IOffsetImm(len, data[0], 2);
        case OP_IOFFSET_S16: if (len < 3) return false; return IOffsetImm(len, ReadUnaligned<int16_t>(data), 3);
        case OP_IOFFSET_U8_LOAD: if (len < 2) return false; return IOffsetLoadImm(len, data[0], 2);
        case OP_IOFFSET_S16_LOAD: if (len < 3) return false; return IOffsetLoadImm(len, ReadUnaligned<int16_t>(data), 3);
        case OP_IOFFSET_U8_STORE: if (len < 2) return false; return IOffsetStoreImm(len, data[0], 2);
        case OP_IOFFSET_S16_STORE: if (len < 3) return false; return IOffsetStoreImm(len, ReadUnaligned<int16_t>(data), 3);
        case OP_LOCAL_U8: if (len < 2) return false; PushLocalAddr(data[0]); len = 2; return true;
        case OP_LOCAL_U16: if (len < 3) return false; PushLocalAddr(ReadUnaligned<uint16_t>(data)); len = 3; return true;
        case OP_LOCAL_U8_LOAD: if (len < 2) return false; Push(m_il.Register(4, LocalTemp(data[0]))); len = 2; return true;
        case OP_LOCAL_U16_LOAD: if (len < 3) return false; Push(m_il.Register(4, LocalTemp(ReadUnaligned<uint16_t>(data)))); len = 3; return true;
        case OP_LOCAL_U8_STORE: if (len < 2) return false; return StoreLocal(data[0], len, 2);
        case OP_LOCAL_U16_STORE: if (len < 3) return false; return StoreLocal(ReadUnaligned<uint16_t>(data), len, 3);
        case OP_STATIC_U8: if (len < 2) return false; PushAddress(StaticAddress(data[0])); len = 2; return true;
        case OP_STATIC_U16: if (len < 3) return false; PushAddress(StaticAddress(ReadUnaligned<uint16_t>(data))); len = 3; return true;
        case OP_STATIC_U24: if (len < 4) return false; PushAddress(StaticAddress(DecodeU24(data))); len = 4; return true;
        case OP_STATIC_U8_LOAD: if (len < 2) return false; Push(m_il.Load(4, m_il.ConstPointer(4, StaticAddress(data[0])))); len = 2; return true;
        case OP_STATIC_U16_LOAD: if (len < 3) return false; Push(m_il.Load(4, m_il.ConstPointer(4, StaticAddress(ReadUnaligned<uint16_t>(data))))); len = 3; return true;
        case OP_STATIC_U24_LOAD: if (len < 4) return false; Push(m_il.Load(4, m_il.ConstPointer(4, StaticAddress(DecodeU24(data))))); len = 4; return true;
        case OP_STATIC_U8_STORE: if (len < 2) return false; return StoreAddress(StaticAddress(data[0]), len, 2);
        case OP_STATIC_U16_STORE: if (len < 3) return false; return StoreAddress(StaticAddress(ReadUnaligned<uint16_t>(data)), len, 3);
        case OP_STATIC_U24_STORE: if (len < 4) return false; return StoreAddress(StaticAddress(DecodeU24(data)), len, 4);
        case OP_GLOBAL_U16: if (len < 3) return false; PushAddress(GlobalAddress(ReadUnaligned<uint16_t>(data))); len = 3; return true;
        case OP_GLOBAL_U24: if (len < 4) return false; PushAddress(GlobalAddress(DecodeU24(data))); len = 4; return true;
        case OP_GLOBAL_U16_LOAD: if (len < 3) return false; Push(m_il.Load(4, m_il.ConstPointer(4, GlobalAddress(ReadUnaligned<uint16_t>(data))))); len = 3; return true;
        case OP_GLOBAL_U24_LOAD: if (len < 4) return false; Push(m_il.Load(4, m_il.ConstPointer(4, GlobalAddress(DecodeU24(data))))); len = 4; return true;
        case OP_GLOBAL_U16_STORE: if (len < 3) return false; return StoreAddress(GlobalAddress(ReadUnaligned<uint16_t>(data)), len, 3);
        case OP_GLOBAL_U24_STORE: if (len < 4) return false; return StoreAddress(GlobalAddress(DecodeU24(data)), len, 4);
        case OP_LOAD: return Load(len);
        case OP_LOAD_N: return LoadN(len);
        case OP_STORE: return Store(len, false);
        case OP_STORE_REV: return Store(len, true);
        case OP_STORE_N: return StoreN(len);
        case OP_ARRAY_U8: if (len < 2) return false; return Array(data[0], len, 2);
        case OP_ARRAY_U16: if (len < 3) return false; return Array(ReadUnaligned<uint16_t>(data), len, 3);
        case OP_ARRAY_U8_LOAD: if (len < 2) return false; return ArrayLoad(data[0], len, 2);
        case OP_ARRAY_U16_LOAD: if (len < 3) return false; return ArrayLoad(ReadUnaligned<uint16_t>(data), len, 3);
        case OP_ARRAY_U8_STORE: if (len < 2) return false; return ArrayStore(data[0], len, 2);
        case OP_ARRAY_U16_STORE: if (len < 3) return false; return ArrayStore(ReadUnaligned<uint16_t>(data), len, 3);
        case OP_STRING: return String(len);
        case OP_STRINGHASH: return StringHash(len);
        case OP_IS_BIT_SET: return IsBitSet(len);
        case OP_TEXT_LABEL_ASSIGN_STRING:
        case OP_TEXT_LABEL_ASSIGN_INT:
        case OP_TEXT_LABEL_APPEND_STRING:
        case OP_TEXT_LABEL_APPEND_INT:
            if (len < 2) return false;
            return PopDiscard(len, 2, 2);
        case OP_TEXT_LABEL_COPY:
            if (len < 2) return false;
            return PopDiscard(len, 3, 2);
        case OP_J:
            return Jump(opcode, addr, len);
        case OP_JZ:
            return Jz(opcode, addr, len);
        case OP_IEQ_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareEqual);
        case OP_INE_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareNotEqual);
        case OP_IGT_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareSignedGreaterThan);
        case OP_IGE_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareSignedGreaterEqual);
        case OP_ILT_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareSignedLessThan);
        case OP_ILE_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareSignedLessEqual);
        case OP_SWITCH:
            return Switch(opcode, addr, len);
        case OP_NATIVE: return Native(opcode, addr, len);
        case OP_CALL: return Call(opcode, addr, len);
        case OP_LEAVE: return Leave(opcode, len);
        default:
            return false;
        }
    }

    void DisableAndFlush()
    {
        for (auto& value : m_stack)
            m_il.AddInstruction(m_il.Push(4, value.expr));
        m_stack.clear();
        m_symbolic = false;
    }

    void Reset()
    {
        m_stack.clear();
        m_symbolic = true;
        m_expectedOutgoingStackDepth.reset();
        m_expectedOutgoingBlockIndices.clear();
        m_expectedFallthroughBlockIndex.reset();
        m_expectedBranchBlockIndex.reset();
        m_explicitFallthroughGoto = false;
    }

    void SetExpectedOutgoingStackDepth(std::optional<size_t> depth, std::vector<uint32_t> targetBlockIndices)
    {
        m_expectedOutgoingStackDepth = depth;
        m_expectedOutgoingBlockIndices = std::move(targetBlockIndices);
        m_expectedBranchBlockIndex.reset();
        m_expectedFallthroughBlockIndex.reset();
        m_explicitFallthroughGoto = false;
    }

    void SetExpectedBranchStackTargets(std::optional<size_t> depth, std::optional<uint32_t> fallthroughBlockIndex,
                                       std::optional<uint32_t> branchBlockIndex, bool explicitFallthroughGoto)
    {
        m_expectedOutgoingStackDepth = depth;
        m_expectedOutgoingBlockIndices.clear();
        m_expectedFallthroughBlockIndex = fallthroughBlockIndex;
        m_expectedBranchBlockIndex = branchBlockIndex;
        m_explicitFallthroughGoto = explicitFallthroughGoto;
    }

    void SeedStack(uint32_t blockIndex, size_t depth)
    {
        m_stack.clear();
        m_symbolic = true;
        m_currentBlockIndex = blockIndex;
        m_seededStackDepth = depth;
        for (size_t i = 0; i < depth; i++)
            Push(m_il.Register(4, StackTemp(blockIndex, static_cast<uint32_t>(i))));
    }

    bool StoreStackOutputs(uint32_t targetBlockIndex, size_t expectedDepth)
    {
        if (m_stack.size() != expectedDepth)
            return false;
        for (size_t i = 0; i < m_stack.size(); i++)
            m_il.AddInstruction(m_il.SetRegister(4, StackTemp(targetBlockIndex, static_cast<uint32_t>(i)), m_stack[i].expr));
        return true;
    }

    bool StoreStackOutputs(size_t expectedDepth)
    {
        if (m_expectedOutgoingBlockIndices.empty())
            return false;
        for (auto targetBlockIndex : m_expectedOutgoingBlockIndices)
        {
            if (!StoreStackOutputs(targetBlockIndex, expectedDepth))
                return false;
        }
        return true;
    }

    size_t StackDepth() const
    {
        return m_stack.size();
    }

  private:
    enum class Kind { Expr, LocalAddr, Address };
    struct Value
    {
        BinaryNinja::ExprId expr = 0;
        Kind kind = Kind::Expr;
        uint32_t index = 0;
        uint64_t address = 0;
        std::optional<uint32_t> constValue;
        std::optional<uint64_t> runtimeDataAddress;
    };

    using BinaryOp = BinaryNinja::ExprId (BinaryNinja::LowLevelILFunction::*)(size_t, BinaryNinja::ExprId, BinaryNinja::ExprId, uint32_t, const BinaryNinja::ILSourceLocation&);
    using CompareOp = BinaryNinja::ExprId (BinaryNinja::LowLevelILFunction::*)(size_t, BinaryNinja::ExprId, BinaryNinja::ExprId, const BinaryNinja::ILSourceLocation&);
    using UnaryOp = BinaryNinja::ExprId (BinaryNinja::LowLevelILFunction::*)(size_t, BinaryNinja::ExprId, uint32_t, const BinaryNinja::ILSourceLocation&);

    void Push(BinaryNinja::ExprId expr, std::optional<uint32_t> constValue = std::nullopt) { m_stack.push_back(Value{expr, Kind::Expr, 0, 0, constValue, std::nullopt}); }
    void PushAddress(uint64_t address) { m_stack.push_back(Value{m_il.ConstPointer(4, address), Kind::Address, 0, address, std::nullopt, std::nullopt}); }
    void PushLocalAddr(uint32_t index) { m_stack.push_back(Value{m_il.Const(4, index), Kind::LocalAddr, index, 0, std::nullopt, std::nullopt}); }
    bool Pop(Value& value)
    {
        if (m_stack.empty())
            return false;
        value = m_stack.back();
        m_stack.pop_back();
        return true;
    }

    bool Dup(size_t& len)
    {
        if (m_stack.empty()) return false;
        m_stack.push_back(m_stack.back()); len = 1; return true;
    }
    bool Drop(size_t& len)
    {
        if (!m_stack.empty())
        {
            Value v;
            Pop(v);
        }
        len = 1;
        return true;
    }
    bool Binary(size_t& len, BinaryOp op)
    {
        Value rhs, lhs;
        if (!Pop(rhs)) return false;
        if (!Pop(lhs))
        {
            uint32_t fallbackSlot = m_seededStackDepth > 0 ? static_cast<uint32_t>(m_seededStackDepth - 1) : 0;
            lhs = Value{m_il.Register(4, StackTemp(m_currentBlockIndex, fallbackSlot)), Kind::Expr, 0, 0, std::nullopt, std::nullopt};
        }
        Push((m_il.*op)(4, lhs.expr, rhs.expr, 0, {})); len = 1; return true;
    }
    bool ImmBinary(size_t& len, uint32_t imm, BinaryOp op, size_t insnLen = 2)
    {
        Value lhs; if (!Pop(lhs)) return false;
        Push((m_il.*op)(4, lhs.expr, m_il.Const(4, imm), 0, {})); len = insnLen; return true;
    }
    bool Unary(size_t& len, UnaryOp op)
    {
        Value v; if (!Pop(v)) return false;
        Push((m_il.*op)(4, v.expr, 0, {})); len = 1; return true;
    }
    template <typename Fn> bool UnaryCustom(size_t& len, Fn fn)
    {
        Value v; if (!Pop(v)) return false;
        Push(fn(v.expr)); len = 1; return true;
    }
    bool IsBitSet(size_t& len)
    {
        Value bit, value;
        if (!Pop(bit) || !Pop(value)) return false;
        Push(m_il.TestBit(4, value.expr, bit.expr));
        len = 1;
        return true;
    }
    bool Compare(size_t& len, CompareOp op)
    {
        Value rhs, lhs; if (!Pop(rhs) || !Pop(lhs)) return false;
        Push((m_il.*op)(4, lhs.expr, rhs.expr, {})); len = 1; return true;
    }
    bool StoreLocal(uint32_t index, size_t& len, size_t insnLen)
    {
        Value v; if (!Pop(v)) return false;
        m_il.AddInstruction(m_il.SetRegister(4, LocalTemp(index), v.expr)); len = insnLen; return true;
    }
    bool StoreAddress(uint64_t address, size_t& len, size_t insnLen)
    {
        Value v; if (!Pop(v)) return false;
        DefineRuntimeDataAddress(address);
        m_il.AddInstruction(m_il.Store(4, m_il.ConstPointer(4, address), v.expr)); len = insnLen; return true;
    }
    bool Load(size_t& len)
    {
        Value ptr; if (!Pop(ptr)) return false;
        if (ptr.kind == Kind::LocalAddr)
            Push(m_il.Register(4, LocalTemp(ptr.index)));
        else if (ptr.kind == Kind::Address)
        {
            DefineRuntimeDataAddress(ptr.address);
            Push(m_il.Load(4, m_il.ConstPointer(4, ptr.address)));
        }
        else
        {
            Push(m_il.Load(4, ptr.expr));
        }
        len = 1; return true;
    }
    bool LoadN(size_t& len)
    {
        Value address, count;
        if (!Pop(address) || !Pop(count) || !count.constValue || *count.constValue > 64)
            return false;
        for (uint32_t i = 0; i < *count.constValue; i++)
        {
            if (address.kind == Kind::LocalAddr)
                Push(m_il.Register(4, LocalTemp(address.index + i)));
            else
            {
                Push(m_il.Load(4, AddOffset(address.expr, static_cast<int64_t>(i) * 4)));
            }
        }
        len = 1;
        return true;
    }
    bool Store(size_t& len, bool reverse)
    {
        Value ptr, val;
        if (reverse)
        {
            if (!Pop(val) || m_stack.empty())
                return false;
            ptr = m_stack.back();
        }
        else
        {
            if (!Pop(ptr) || !Pop(val))
                return false;
        }
        if (ptr.kind == Kind::LocalAddr)
            m_il.AddInstruction(m_il.SetRegister(4, LocalTemp(ptr.index), val.expr));
        else if (ptr.kind == Kind::Address)
        {
            DefineRuntimeDataAddress(ptr.address);
            m_il.AddInstruction(m_il.Store(4, m_il.ConstPointer(4, ptr.address), val.expr));
        }
        else
        {
            m_il.AddInstruction(m_il.Store(4, ptr.expr, val.expr));
        }
        len = 1; return true;
    }
    bool StoreN(size_t& len)
    {
        Value address, count;
        if (!Pop(address) || !Pop(count) || !count.constValue || *count.constValue > 64)
            return false;
        for (uint32_t i = 0; i < *count.constValue; i++)
        {
            Value value;
            if (!Pop(value)) return false;
            uint32_t reverseIndex = *count.constValue - i - 1;
            if (address.kind == Kind::LocalAddr)
                m_il.AddInstruction(m_il.SetRegister(4, LocalTemp(address.index + reverseIndex), value.expr));
            else
            {
                m_il.AddInstruction(m_il.Store(4, AddOffset(address.expr, static_cast<int64_t>(reverseIndex) * 4), value.expr));
            }
        }
        len = 1;
        return true;
    }
    BinaryNinja::ExprId ArrayElementAddress(const Value& address, const Value& index, uint32_t stride)
    {
        if (address.kind == Kind::Address)
        {
            uint64_t dataAddress = address.address + 4;
            DefineRuntimeArrayDataAddress(address.address, dataAddress, stride);
            return m_il.Add(4, RuntimePointer(dataAddress), m_il.Mult(4, m_il.Const(4, stride * 4), index.expr));
        }
        BinaryNinja::ExprId cellOffset = m_il.Add(4, m_il.Const(4, 1), m_il.Mult(4, m_il.Const(4, stride), index.expr));
        return m_il.Add(4, address.expr, m_il.Mult(4, cellOffset, m_il.Const(4, 4)));
    }
    Value ArrayElementValue(const Value& address, const Value& index, uint32_t stride)
    {
        if (address.kind == Kind::Address)
        {
            uint64_t dataAddress = address.address + 4;
            DefineRuntimeArrayDataAddress(address.address, dataAddress, stride);
            return Value{m_il.Add(4, RuntimePointer(dataAddress), m_il.Mult(4, m_il.Const(4, stride * 4), index.expr)), Kind::Expr, 0, 0, std::nullopt, dataAddress};
        }
        return Value{ArrayElementAddress(address, index, stride), Kind::Expr, 0, 0, std::nullopt, address.runtimeDataAddress};
    }
    bool Array(uint32_t stride, size_t& len, size_t insnLen)
    {
        Value address, index;
        if (!Pop(address) || !Pop(index)) return false;
        m_stack.push_back(ArrayElementValue(address, index, stride));
        len = insnLen;
        return true;
    }
    bool ArrayLoad(uint32_t stride, size_t& len, size_t insnLen)
    {
        Value address, index;
        if (!Pop(address) || !Pop(index)) return false;
        auto elem = ArrayElementValue(address, index, stride);
        Push(m_il.Load(4, elem.expr));
        len = insnLen;
        return true;
    }
    bool ArrayStore(uint32_t stride, size_t& len, size_t insnLen)
    {
        Value address, index, value;
        if (!Pop(address) || !Pop(index) || !Pop(value)) return false;
        auto elem = ArrayElementValue(address, index, stride);
        m_il.AddInstruction(m_il.Store(4, elem.expr, value.expr));
        len = insnLen;
        return true;
    }
    BinaryNinja::ExprId AddOffset(BinaryNinja::ExprId base, int64_t offsetBytes)
    {
        if (offsetBytes < 0)
            return m_il.Sub(4, base, m_il.Const(4, static_cast<uint64_t>(-offsetBytes)));
        return m_il.Add(4, base, m_il.Const(4, static_cast<uint64_t>(offsetBytes)));
    }
    BinaryNinja::ExprId RuntimePointer(uint64_t address)
    {
        return m_il.ConstPointer(4, address);
    }
    bool IOffset(size_t& len)
    {
        Value base, index;
        if (!Pop(base) || !Pop(index)) return false;
        m_stack.push_back(Value{m_il.Add(4, base.expr, m_il.Mult(4, index.expr, m_il.Const(4, 4))), Kind::Expr, 0, 0, std::nullopt, base.runtimeDataAddress});
        len = 1;
        return true;
    }
    bool IOffsetImm(size_t& len, int64_t operand, size_t insnLen)
    {
        Value base;
        if (!Pop(base)) return false;
        int64_t offsetBytes = operand * 4;
        if (base.kind == Kind::Address)
            PushAddress(static_cast<uint64_t>(static_cast<int64_t>(base.address) + offsetBytes));
        else
        {
            if (base.runtimeDataAddress)
                DefineRuntimeArrayOffsetAlias(*base.runtimeDataAddress, offsetBytes);
            auto expr = AddOffset(base.expr, offsetBytes);
            m_stack.push_back(Value{expr, Kind::Expr, 0, 0, std::nullopt, base.runtimeDataAddress});
        }
        len = insnLen;
        return true;
    }
    bool IOffsetLoadImm(size_t& len, int64_t operand, size_t insnLen)
    {
        Value base;
        if (!Pop(base)) return false;
        int64_t offsetBytes = operand * 4;
        if (base.kind == Kind::Address)
        {
            uint64_t address = static_cast<uint64_t>(static_cast<int64_t>(base.address) + offsetBytes);
            DefineRuntimeDataAddress(address);
            Push(m_il.Load(4, m_il.ConstPointer(4, address)));
        }
        else
        {
            if (base.runtimeDataAddress)
                DefineRuntimeArrayOffsetAlias(*base.runtimeDataAddress, offsetBytes);
            Push(m_il.Load(4, AddOffset(base.expr, offsetBytes)));
        }
        len = insnLen;
        return true;
    }
    bool IOffsetStoreImm(size_t& len, int64_t operand, size_t insnLen)
    {
        Value base, value;
        if (!Pop(base) || !Pop(value)) return false;
        int64_t offsetBytes = operand * 4;
        if (base.kind == Kind::Address)
        {
            uint64_t address = static_cast<uint64_t>(static_cast<int64_t>(base.address) + offsetBytes);
            DefineRuntimeDataAddress(address);
            m_il.AddInstruction(m_il.Store(4, m_il.ConstPointer(4, address), value.expr));
        }
        else
        {
            if (base.runtimeDataAddress)
                DefineRuntimeArrayOffsetAlias(*base.runtimeDataAddress, offsetBytes);
            m_il.AddInstruction(m_il.Store(4, AddOffset(base.expr, offsetBytes), value.expr));
        }
        len = insnLen;
        return true;
    }
    bool PopDiscard(size_t& len, size_t count, size_t insnLen)
    {
        for (size_t i = 0; i < count; i++)
        {
            Value v;
            if (!Pop(v)) return false;
        }
        m_il.AddInstruction(m_il.Nop());
        len = insnLen;
        return true;
    }
    bool String(size_t& len)
    {
        Value off; if (!Pop(off)) return false;
        auto section = m_view ? m_view->GetSectionByName("STRINGS") : nullptr;
        if (!section) return false;
        Push(m_il.Add(4, m_il.ConstPointer(4, section->GetStart()), off.expr)); len = 1; return true;
    }
    bool StringHash(size_t& len)
    {
        Value str; if (!Pop(str)) return false;
        uint32_t resultTemp = CallResultTemp(m_il.GetCurrentAddress());
        m_il.AddInstruction(m_il.Intrinsic({BinaryNinja::RegisterOrFlag::Register(resultTemp)}, Intrin_StringHash, {str.expr}));
        Push(m_il.Register(4, resultTemp));
        len = 1;
        return true;
    }
    bool Jump(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (len < 3) return false;
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;
        EmitGoto(BranchTarget(addr, opcode)); len = 3; return true;
    }
    bool Jz(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        Value cond; if (len < 3 || !Pop(cond)) return false;
        uint64_t branchTarget = BranchTarget(addr, opcode);
        uint64_t fallthroughTarget = addr + 3;
        if (branchTarget == fallthroughTarget)
        {
            if (m_expectedOutgoingStackDepth)
            {
                if (m_expectedFallthroughBlockIndex)
                {
                    if (!StoreStackOutputs(*m_expectedFallthroughBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (m_expectedBranchBlockIndex)
                {
                    if (!StoreStackOutputs(*m_expectedBranchBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (!StoreStackOutputs(*m_expectedOutgoingStackDepth))
                    return false;
            }
            len = 3;
            return true;
        }
        if (cond.constValue)
        {
            bool takeBranch = *cond.constValue == 0;
            uint64_t liveTarget = takeBranch ? branchTarget : fallthroughTarget;
            if (m_expectedOutgoingStackDepth)
            {
                auto liveBlockIndex = takeBranch ? m_expectedBranchBlockIndex : m_expectedFallthroughBlockIndex;
                if (liveBlockIndex)
                {
                    if (!StoreStackOutputs(*liveBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (!StoreStackOutputs(*m_expectedOutgoingStackDepth))
                    return false;
            }
            EmitGoto(liveTarget);
            len = 3;
            return true;
        }
        if (m_expectedOutgoingStackDepth && m_expectedBranchBlockIndex && m_expectedFallthroughBlockIndex)
        {
            EmitJzWithEdgeStackStores(cond.expr, branchTarget, fallthroughTarget, *m_expectedBranchBlockIndex,
                                      *m_expectedFallthroughBlockIndex, *m_expectedOutgoingStackDepth, m_explicitFallthroughGoto);
            len = 3;
            return true;
        }
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;
        EmitJz(cond.expr, branchTarget); len = 3; return true;
    }
    bool CompareJz(const uint8_t* opcode, uint64_t addr, size_t& len, CompareOp op)
    {
        Value rhs, lhs; if (len < 3 || !Pop(rhs) || !Pop(lhs)) return false;
        auto cond = (m_il.*op)(4, lhs.expr, rhs.expr, {});
        uint64_t branchTarget = BranchTarget(addr, opcode);
        uint64_t fallthroughTarget = addr + 3;
        if (branchTarget == fallthroughTarget)
        {
            if (m_expectedOutgoingStackDepth)
            {
                if (m_expectedFallthroughBlockIndex)
                {
                    if (!StoreStackOutputs(*m_expectedFallthroughBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (m_expectedBranchBlockIndex)
                {
                    if (!StoreStackOutputs(*m_expectedBranchBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (!StoreStackOutputs(*m_expectedOutgoingStackDepth))
                    return false;
            }
            len = 3;
            return true;
        }
        if (m_expectedOutgoingStackDepth && m_expectedBranchBlockIndex && m_expectedFallthroughBlockIndex)
        {
            EmitJzWithEdgeStackStores(cond, branchTarget, fallthroughTarget, *m_expectedBranchBlockIndex,
                                      *m_expectedFallthroughBlockIndex, *m_expectedOutgoingStackDepth, m_explicitFallthroughGoto);
            len = 3;
            return true;
        }
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;
        EmitJz(cond, branchTarget); len = 3; return true;
    }
    bool Switch(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (len < 2) return false;
        Value selector; if (!Pop(selector)) return false;
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;
        uint8_t count = opcode[1];
        std::vector<SwitchCase> cases(count);
        if (!m_view || m_view->Read(cases.data(), addr + 2, cases.size() * sizeof(SwitchCase)) < cases.size() * sizeof(SwitchCase))
            return false;

        std::vector<BinaryNinja::LowLevelILLabel> falseLabels(count);
        for (size_t i = 0; i < cases.size(); i++)
        {
            BinaryNinja::LowLevelILLabel trueLabel;
            m_il.AddInstruction(m_il.If(m_il.CompareEqual(4, selector.expr, m_il.Const(4, cases[i].m_case)), trueLabel, falseLabels[i]));
            m_il.MarkLabel(trueLabel);
            EmitGoto(static_cast<uint64_t>(static_cast<int64_t>(addr) + cases[i].m_target + static_cast<int64_t>((i + 1) * 6 + 2)));
            m_il.MarkLabel(falseLabels[i]);
        }
        EmitGoto(addr + 2 + cases.size() * sizeof(SwitchCase));
        len = 2 + cases.size() * sizeof(SwitchCase);
        return true;
    }
    bool Native(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (len < 4) return false;
        uint8_t retCount = opcode[1] & 3;
        uint8_t paramCount = (opcode[1] >> 2) & 0x3f;
        if (paramCount > 16)
            return false;
        std::vector<Value> args;
        for (uint8_t i = 0; i < paramCount; i++) { Value v; if (!Pop(v)) return false; args.push_back(v); }
        uint32_t argIndex = 0;
        for (auto it = args.rbegin(); it != args.rend(); ++it, ++argIndex)
            m_il.AddInstruction(m_il.SetRegister(4, ArgReg(argIndex), it->expr));
        auto nativeSection = m_view ? m_view->GetSectionByName("NATIVES") : nullptr;
        if (!nativeSection) return false;
        uint64_t nativeAddress = nativeSection->GetStart() + static_cast<uint64_t>((opcode[2] << 8) | opcode[3]) * 8;
        m_il.AddInstruction(m_il.Call(m_il.ExternPointer(8, nativeAddress, 0)));
        if (m_view)
        {
            if (auto symbol = m_view->GetSymbolByAddress(nativeAddress))
            {
                if (symbol->GetRawName() == "native_SCRIPT_TERMINATE_THIS_THREAD")
                    m_il.AddInstruction(m_il.NoReturn());
            }
        }
        for (uint8_t i = 0; i < retCount; i++)
        {
            uint32_t resultTemp = CallResultTemp(addr + i);
            m_il.AddInstruction(m_il.SetRegister(4, resultTemp, m_il.Register(4, ReturnReg(i))));
            Push(m_il.Register(4, resultTemp));
        }
        len = 4;
        return true;
    }
    bool Call(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (len < 4) return false;
        auto code = m_view ? m_view->GetSectionByName("CODE") : nullptr;
        if (!code) return false;
        uint64_t target = code->GetStart() + DecodeU24(opcode + 1);
        uint8_t paramCount = GetEnterParamCount(m_view, target);
        uint8_t retCount = FindFirstLeaveReturnCount(m_view, target);
        if (paramCount > 16)
            return false;
        std::vector<Value> args;
        for (uint8_t i = 0; i < paramCount; i++) { Value v; if (!Pop(v)) return false; args.push_back(v); }
        uint32_t argIndex = 0;
        for (auto it = args.rbegin(); it != args.rend(); ++it, ++argIndex)
            m_il.AddInstruction(m_il.SetRegister(4, ArgReg(argIndex), it->expr));
        m_il.AddInstruction(m_il.Call(m_il.ConstPointer(4, target)));
        for (uint8_t i = 0; i < retCount; i++)
        {
            uint32_t resultTemp = CallResultTemp(addr + i);
            m_il.AddInstruction(m_il.SetRegister(4, resultTemp, m_il.Register(4, ReturnReg(i))));
            Push(m_il.Register(4, resultTemp));
        }
        len = 4;
        return true;
    }
    bool Leave(const uint8_t* opcode, size_t& len)
    {
        if (len < 3) return false;
        uint8_t retCount = opcode[2];
        if (retCount > 0)
        {
            Value ret; if (!Pop(ret)) return false;
            m_il.AddInstruction(m_il.SetRegister(4, Reg_R1, ret.expr));
        }
        m_il.AddInstruction(m_il.Return(m_il.ConstPointer(4, 0)));
        len = 3;
        return true;
    }

    bool Enter(const uint8_t* opcode, size_t& len)
    {
        if (len < 5) return false;
        uint8_t paramCount = opcode[1];
        uint8_t nameLen = opcode[4];
        if (len < static_cast<size_t>(5 + nameLen)) return false;
        if (paramCount > 16) return false;
        m_stack.clear();
        for (uint32_t i = 0; i < paramCount; i++)
            m_il.AddInstruction(m_il.SetRegister(4, LocalTemp(i), m_il.Register(4, ArgReg(i))));
        m_il.AddInstruction(m_il.Nop());
        len = 5 + nameLen;
        return true;
    }

    void EmitGoto(uint64_t target)
    {
        if (auto label = m_il.GetLabelForAddress(m_arch, target))
            m_il.AddInstruction(m_il.Goto(*label));
        else
            m_il.AddInstruction(m_il.Jump(m_il.ConstPointer(4, target)));
    }
    void EmitJz(BinaryNinja::ExprId cond, uint64_t falseTarget, std::optional<uint64_t> trueTarget = std::nullopt)
    {
        BinaryNinja::LowLevelILLabel trueLabel;
        BinaryNinja::LowLevelILLabel falseLabel;
        m_il.AddInstruction(m_il.If(cond, trueLabel, falseLabel));
        m_il.MarkLabel(falseLabel);
        EmitGoto(falseTarget);
        m_il.MarkLabel(trueLabel);
        if (trueTarget)
            EmitGoto(*trueTarget);
    }

    void EmitJzWithEdgeStackStores(BinaryNinja::ExprId cond, uint64_t falseTarget, uint64_t trueTarget,
                                   uint32_t falseTargetBlockIndex, uint32_t trueTargetBlockIndex, size_t expectedDepth,
                                   bool explicitTrueTarget)
    {
        // For YSC short-circuit expressions, the same VM-stack value is available on both
        // successors immediately after the conditional pop. Emitting the successor-input
        // stores before the branch gives BN a simple "temp = cond; if (!temp) temp |= rhs"
        // shape instead of edge-local assignments that HLIL tends to linearize as
        // confusing overwrites.
        StoreStackOutputs(falseTargetBlockIndex, expectedDepth);
        StoreStackOutputs(trueTargetBlockIndex, expectedDepth);
        EmitJz(cond, falseTarget, explicitTrueTarget ? std::optional<uint64_t>(trueTarget) : std::nullopt);
    }
    uint64_t StaticAddress(uint32_t operand)
    {
        auto section = m_view ? m_view->GetSectionByName("STATICS") : nullptr;
        return (section ? section->GetStart() : 0) + operand * 4;
    }
    uint64_t GlobalAddress(uint32_t operand)
    {
        auto section = m_view ? m_view->GetSectionByName("GLOBALS") : nullptr;
        uint32_t block = operand >> 18;
        uint32_t needle = operand & 0x3ffff;
        uint64_t address = (section ? section->GetStart() : 0) + (static_cast<uint64_t>(block) * (1 << 18) + needle) * 4;
        if (m_view)
        {
            m_view->DefineDataVariable(address, VolatileInt32Type());
            m_view->DefineAutoSymbol(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, fmt::format("Global_{}", operand), address));
        }
        return address;
    }

    void DefineRuntimeDataAddress(uint64_t address)
    {
        if (!m_view)
            return;
        auto globals = m_view->GetSectionByName("GLOBALS");
        if (globals && address >= globals->GetStart() && address < globals->GetEnd())
        {
            uint64_t index = (address - globals->GetStart()) / 4;
            m_view->DefineDataVariable(address, VolatileInt32Type());
            m_view->DefineAutoSymbol(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, fmt::format("Global_{}", index), address));
        }
    }

    inline static std::mutex g_runtimeArrayShapeMutex;
    inline static std::set<uint64_t> g_definedRuntimeArrayBases;
    inline static std::set<uint64_t> g_definedRuntimeArrayOffsetAliases;

    uint64_t RuntimeArrayKey(uint64_t dataAddress, uint32_t stride) const
    {
        return (dataAddress << 16) ^ stride;
    }

    void DefineRuntimeArrayDataAddress(uint64_t headerAddress, uint64_t dataAddress, uint32_t stride)
    {
        std::lock_guard<std::mutex> guard(g_runtimeArrayShapeMutex);
        DefineRuntimeArrayDataAddressLocked(headerAddress, dataAddress, stride);
    }

    void DefineRuntimeArrayDataAddressLocked(uint64_t headerAddress, uint64_t dataAddress, uint32_t stride)
    {
        if (!m_view || stride == 0 || stride > 4096)
            return;

        uint64_t key = RuntimeArrayKey(dataAddress, stride);
        if (!g_definedRuntimeArrayBases.insert(key).second)
            return;

        auto globals = m_view->GetSectionByName("GLOBALS");
        auto statics = m_view->GetSectionByName("STATICS");
        BinaryNinja::Section* section = nullptr;
        std::string prefix;
        if (globals && dataAddress >= globals->GetStart() && dataAddress < globals->GetEnd())
        {
            section = globals;
            prefix = "Global";
        }
        else if (statics && dataAddress >= statics->GetStart() && dataAddress < statics->GetEnd())
        {
            section = statics;
            prefix = "Static";
        }
        if (!section)
            return;

        uint64_t headerIndex = (headerAddress - section->GetStart()) / 4;
        m_view->DefineDataVariable(dataAddress, VolatileInt32Type());
        m_view->DefineAutoSymbol(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, fmt::format("{}_{}_data", prefix, headerIndex), dataAddress));
    }

    void DefineRuntimeArrayOffsetAlias(uint64_t dataAddress, int64_t offsetBytes)
    {
        if (!m_view || offsetBytes < 0 || offsetBytes > 0x100000)
            return;
        uint64_t aliasAddress = dataAddress + static_cast<uint64_t>(offsetBytes);
        std::lock_guard<std::mutex> guard(g_runtimeArrayShapeMutex);
        if (!g_definedRuntimeArrayOffsetAliases.insert(aliasAddress).second)
            return;

        auto globals = m_view->GetSectionByName("GLOBALS");
        auto statics = m_view->GetSectionByName("STATICS");
        BinaryNinja::Section* section = nullptr;
        std::string prefix;
        if (globals && dataAddress >= globals->GetStart() && dataAddress < globals->GetEnd())
        {
            section = globals;
            prefix = "Global";
        }
        else if (statics && dataAddress >= statics->GetStart() && dataAddress < statics->GetEnd())
        {
            section = statics;
            prefix = "Static";
        }
        if (!section || aliasAddress >= section->GetEnd())
            return;

        uint64_t dataIndex = (dataAddress - section->GetStart()) / 4;
        uint64_t headerIndex = dataIndex > 0 ? dataIndex - 1 : dataIndex;
        m_view->DefineDataVariable(aliasAddress, VolatileInt32Type());
        m_view->DefineAutoSymbol(new BinaryNinja::Symbol(
            BNSymbolType::DataSymbol, fmt::format("{}_{}_data_plus_{:x}", prefix, headerIndex, static_cast<uint64_t>(offsetBytes)), aliasAddress));
    }

    YSCArchitecture* m_arch;
    BinaryNinja::LowLevelILFunction& m_il;
    BinaryNinja::BinaryView* m_view;
    std::vector<Value> m_stack;
    bool m_symbolic = true;
    uint32_t m_currentBlockIndex = 0;
    size_t m_seededStackDepth = 0;
    std::optional<size_t> m_expectedOutgoingStackDepth;
    std::vector<uint32_t> m_expectedOutgoingBlockIndices;
    std::optional<uint32_t> m_expectedFallthroughBlockIndex;
    std::optional<uint32_t> m_expectedBranchBlockIndex;
    bool m_explicitFallthroughGoto = false;
};
}

std::string YSCArchitecture::GetRegisterName(uint32_t reg)
{
    if (reg >= Reg_MAX)
        return "UNKREG";
    return std::string(g_RegNames[reg]);
}


bool YSCArchitecture::GetInstructionInfo(const uint8_t* data, uint64_t addr, size_t maxLen,
                                         BinaryNinja::InstructionInfo& result)
{
    if (maxLen < 1)
        return false;
    uint8_t insn = data[0];
    if (insn >= OP_MAX)
        return false;
    size_t instrLen = GetRawInstructionLength(nullptr, addr, data, maxLen);
    if (instrLen == 0 || maxLen < instrLen)
        return false;
    m_insns[insn]->GetInstructionInfo(data + 1, addr, maxLen, result);
    result.length = instrLen;
    return true;
}


bool YSCArchitecture::GetInstructionText(const uint8_t* data, uint64_t addr, size_t& len,
                                         std::vector<BinaryNinja::InstructionTextToken>& result)
{
    uint8_t insn = data[0];
    if (insn >= OP_MAX)
        return false;
    size_t instrLen = GetRawInstructionLength(nullptr, addr, data, len);
    if (instrLen == 0 || len < instrLen)
        return false;
    len = instrLen;
    m_insns[insn]->GetInstructionText(data + 1, addr, len, result);
    len = instrLen;
    return true;
}

bool YSCArchitecture::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len,
                                               BinaryNinja::LowLevelILFunction& il)
{
    if (len < 1)
        return false;
    uint8_t insn = data[0];
    if (insn >= OP_MAX)
        return false;
    size_t instrLen = GetRawInstructionLength(nullptr, addr, data, len);
    if (instrLen == 0 || len < instrLen)
        return false;

    m_insns[insn]->GetInstructionLowLevelIL(data + 1, addr, len, il);
    if (!m_insns[insn]->CustomLLILSize())
        len = instrLen;
    return true;
}

BNRegisterInfo YSCArchitecture::GetRegisterInfo(uint32_t reg)
{
    BNRegisterInfo info;
    if (reg < Reg_MAX)
        info.fullWidthRegister = reg;
    info.size = 4;
    info.extend = NoExtend;
    info.offset = 0;
    return info;
}

uint32_t YSCArchitecture::GetStackPointerRegister()
{
    return Reg_SP;
}

std::vector<uint32_t> YSCArchitecture::GetAllRegisters()
{
    std::vector<uint32_t> result;
    for (uint32_t i = 0; i < Reg_MAX; i++)
        result.push_back(i);
    return result;
}

BNIntrinsicClass YSCArchitecture::GetIntrinsicClass(uint32_t intrinsic)
{
    return BNIntrinsicClass::GeneralIntrinsicClass;
}

std::string YSCArchitecture::GetIntrinsicName(uint32_t intrinsic)
{
    if (intrinsic >= Intrin_MAX)
    {
        return "UNKINTRIN";
    }

    return std::string(g_intrinNames[intrinsic]);
}

std::vector<uint32_t> YSCArchitecture::GetAllIntrinsics()
{
    std::vector<uint32_t> result;
    for (uint32_t i = 0; i < Intrin_MAX; i++)
        result.push_back(i);
    return result;
}

std::vector<BinaryNinja::NameAndType> YSCArchitecture::GetIntrinsicInputs(uint32_t intrinsic)
{
    using namespace BinaryNinja;
    std::vector<NameAndType> result;
    switch (intrinsic)
    {
    case Intrin_StringHash:
        result.emplace_back("str", Type::PointerType(4, Type::IntegerType(1, false)));
        break;
    default:
        break;
    }
    return result;
}

std::vector<BinaryNinja::Confidence<BinaryNinja::Ref<BinaryNinja::Type>>>
YSCArchitecture::GetIntrinsicOutputs(uint32_t intrinsic)
{
    using namespace BinaryNinja;
    std::vector<Confidence<Ref<Type>>> result;
    switch (intrinsic)
    {
    case Intrin_StringHash:
        result.emplace_back(Type::IntegerType(4, false));
        break;
    default:
        break;
    }
    return result;
}

YSCBlockAnalysisContext::YSCBlockAnalysisContext(BinaryNinja::Function* function,
                                                 BinaryNinja::BasicBlockAnalysisContext* ctx)
    : m_function(function), m_ctx(ctx)
{
    m_functionContext = new YSCFunctionContext();
    m_functionContext->m_start = function->GetStart();
    ctx->SetFunctionArchContextRaw(m_functionContext);
    m_blocksToProcess.push(function->GetStart());
    m_processingBlocks.insert(function->GetStart());
}

/**
 * @brief Analyzes the basic blocks of a given function within the specified analysis context.
 */
void YSCArchitecture::AnalyzeBasicBlocks(BinaryNinja::Function* function, BinaryNinja::BasicBlockAnalysisContext& ctx)
{
    YSCBlockAnalysisContext analysisCtx(function, &ctx);
    if (!analysisCtx.IsFirstInstructionEnter())
    {
        auto view = analysisCtx.GetView();
        uint64_t start = function->GetStart();
        uint8_t byte = 0;
        view->Read(&byte, start, 1);

        BinaryNinja::Ref<BinaryNinja::BasicBlock> block = ctx.CreateBasicBlock(function->GetArchitecture(), start);
        block->AddInstructionData(&byte, 1);
        block->SetHasInvalidInstructions(true);
        block->SetEnd(start + 1);
        ctx.AddFunctionBasicBlock(block);
        ctx.Finalize();
        return;
    }

    auto view = analysisCtx.GetView();
    uint64_t totalSize = 0;
    uint64_t maxSize = ctx.GetMaxFunctionSize();
    bool maxSizeReached = false;

    while (analysisCtx.IsProcessing())
    {
        uint64_t currentAddr = analysisCtx.PopNextBlock();

        // Skip if this block has already been processed
        if (analysisCtx.HasSeenBlock(currentAddr))
            continue;

        analysisCtx.MarkBlockAsSeen(currentAddr);

        BinaryNinja::Ref<BinaryNinja::BasicBlock> block =
            ctx.CreateBasicBlock(function->GetArchitecture(), currentAddr);
        analysisCtx.AddBlock(currentAddr, block);

        uint64_t blockStartAddr = currentAddr;

        while (true)
        {
            uint8_t opcode[BN_MAX_INSTRUCTION_LENGTH] = {};
            size_t maxLen = view->Read(opcode, currentAddr, GetMaxInstructionLength());
            if (maxLen < 1)
            {
                block->SetHasInvalidInstructions(true);
                break;
            }

            uint8_t insn = opcode[0];
            if (insn >= OP_MAX)
            {
                block->SetHasInvalidInstructions(true);
                break;
            }

            size_t instrLen = GetRawInstructionLength(view, currentAddr, opcode, maxLen);
            if (instrLen == 0 || instrLen > maxLen)
            {
                block->SetHasInvalidInstructions(true);
                break;
            }
            BinaryNinja::InstructionInfo info;
            if (GetInstructionInfo(opcode, currentAddr, maxLen, info))
            {
                for (size_t i = 0; i < info.branchCount; i++)
                {
                    if (info.branchType[i] != BNBranchType::CallDestination)
                        continue;

                    uint64_t target = info.branchTarget[i];
                    ctx.GetDirectCodeReferences()[target].emplace(function->GetArchitecture(), currentAddr);
                    if (view->IsOffsetExternSemantics(target))
                        continue;

                    if (!view->IsValidOffset(target) || !view->IsOffsetBackedByFile(target))
                        continue;
                    if (!IsEnterAt(view, target))
                        continue;

                    auto platform = function->GetPlatform();
                    auto callee = view->AddFunctionForAnalysis(platform, target, true);
                    if (callee)
                        ctx.AddTempOutgoingReference(callee);
                }
            }

            size_t bytesRead = 0;
            bool endsBlock = m_insns[insn]->GetInstructionBlockAnalysis(analysisCtx, currentAddr, bytesRead);
            if (endsBlock && insn == OP_LEAVE && instrLen >= 3)
                analysisCtx.RecordReturnCount(opcode[2]);
            if (bytesRead == 0)
            {
                analysisCtx.AddCurrentInstructionData(opcode, instrLen);
                bytesRead = instrLen;
            }
            else if (bytesRead != instrLen)
            {
                // Some legacy per-op block analyzers still use fixed GetSize() values
                // for variable-size instructions or have operand-offset bugs. Basic
                // block boundaries must be based on the canonical raw decoder so the
                // new whole-function lifter receives non-overlapping instruction ranges.
                bytesRead = instrLen;
            }

            if (bytesRead == 0)
            {
                // If no bytes were read, something went wrong - end the block
                block->SetHasInvalidInstructions(true);
                break;
            }

            if (currentAddr + bytesRead < currentAddr || currentAddr + bytesRead > view->GetEnd()) // overflow check
            {
                block->SetHasInvalidInstructions(true);
                break;
            }

            currentAddr += bytesRead;
            totalSize += bytesRead;

            auto analysisSkipOverride = ctx.GetAnalysisSkipOverride();
            uint64_t effectiveMaxSize = maxSize;
            if (analysisSkipOverride == NeverSkipFunctionAnalysis)
                effectiveMaxSize = 0;
            else if (!effectiveMaxSize && (analysisSkipOverride == AlwaysSkipFunctionAnalysis))
                effectiveMaxSize = ctx.GetMaxFunctionSize();

            if (effectiveMaxSize && totalSize > effectiveMaxSize)
            {
                maxSizeReached = true;
                break;
            }

            // Check if we've hit the start of another block that's already been seen or is being processed
            if (!endsBlock && currentAddr != blockStartAddr &&
                (analysisCtx.IsBlockProcessing(currentAddr) || analysisCtx.HasSeenBlock(currentAddr)))
            {
                endsBlock = true;
                block->AddPendingOutgoingEdge(BNBranchType::UnconditionalBranch, currentAddr, nullptr, true);
            }

            if (endsBlock)
                break;
        }

        // Only add the block if it has instructions (currentAddr moved from start)
        if (currentAddr > blockStartAddr)
        {
            block->SetEnd(currentAddr);
            ctx.AddFunctionBasicBlock(block);
        }

        analysisCtx.MarkBlockAsProcessed(blockStartAddr);

        if (maxSizeReached)
            break;
    }

    if (maxSizeReached)
        ctx.SetMaxSizeReached(true);

    ApplyYSCFunctionType(function, view, analysisCtx.GetFunctionContext()->m_enter,
                         analysisCtx.GetFunctionContext()->m_returnCount);
    ctx.Finalize();
}

bool YSCArchitecture::LiftFunction(BinaryNinja::LowLevelILFunction* function,
                                   BinaryNinja::FunctionLifterContext& context)
{
    auto view = context.GetView();
    auto blocks = context.GetBasicBlocks();
    YSCSymbolicLifter symbolicLifter(this, *function, view);
    std::map<uint64_t, size_t> vmInputDepths;
    std::map<uint64_t, uint32_t> vmBlockIndices;
    std::map<uint64_t, uint64_t> nextBlockStarts;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        vmBlockIndices[blocks[i]->GetStart()] = static_cast<uint32_t>(i);
        if (i + 1 < blocks.size())
            nextBlockStarts[blocks[i]->GetStart()] = blocks[i + 1]->GetStart();
    }
    YSCLiftDiagnostic canDiagnostic;
    YSCLiftDiagnostic stackDiagnostic;
    auto owner = function->GetFunction();
    uint64_t functionStart = owner ? owner->GetStart() : (blocks.empty() ? 0 : blocks.front()->GetStart());
    bool startsWithEnter = IsEnterAt(view, functionStart);
    bool canWholeFunctionLift = CanUseWholeFunctionStackLifting(view, blocks, GetMaxInstructionLength(), &canDiagnostic);
    if (!startsWithEnter && canWholeFunctionLift)
    {
        canDiagnostic.reason = "non-enter-function";
        canDiagnostic.address = functionStart;
        canWholeFunctionLift = false;
    }
    bool stackAnalysisOk = canWholeFunctionLift && AnalyzeFunctionVMStack(view, blocks, GetMaxInstructionLength(), vmInputDepths, functionStart, &stackDiagnostic);
    bool useFunctionLevelStackLifting = canWholeFunctionLift && stackAnalysisOk;
    if (useFunctionLevelStackLifting && blocks.size() >= 16)
    {
        std::string functionName = owner && owner->GetSymbol() ? owner->GetSymbol()->GetShortName() : "<unknown>";
        BinaryNinja::LogInfo("YSC new-lift decision for %s @ %#llx: canWhole=1 stackOk=1 use=1 blocks=%zu reason=ok addr=0 target=0 opcode=INVALID depth=0 otherDepth=0 totalSize=0 limit=%llu",
                             functionName.c_str(), static_cast<unsigned long long>(functionStart), blocks.size(),
                             static_cast<unsigned long long>(YSC_WHOLE_FUNCTION_LIFT_SIZE_LIMIT));
    }
    else if (!useFunctionLevelStackLifting && startsWithEnter)
    {
        std::string functionName = owner && owner->GetSymbol() ? owner->GetSymbol()->GetShortName() : "<unknown>";
        const auto& diag = canWholeFunctionLift ? stackDiagnostic : canDiagnostic;
        BinaryNinja::LogInfo("YSC new-lift decision for %s @ %#llx: canWhole=%d stackOk=%d use=%d blocks=%zu reason=%s addr=%#llx target=%#llx opcode=%s depth=%d otherDepth=%d totalSize=%zu limit=%llu",
                             functionName.c_str(), static_cast<unsigned long long>(functionStart), canWholeFunctionLift ? 1 : 0,
                             stackAnalysisOk ? 1 : 0, useFunctionLevelStackLifting ? 1 : 0, blocks.size(), diag.reason.c_str(),
                             static_cast<unsigned long long>(diag.address), static_cast<unsigned long long>(diag.target),
                             OpcodeName(diag.opcode), diag.depth, diag.otherDepth, diag.totalSize,
                             static_cast<unsigned long long>(YSC_WHOLE_FUNCTION_LIFT_SIZE_LIMIT));
    }

    auto isTerminal = [](const BinaryNinja::LowLevelILInstruction& instr) {
        switch (instr.operation)
        {
        case LLIL_GOTO:
        case LLIL_JUMP:
        case LLIL_JUMP_TO:
        case LLIL_RET:
        case LLIL_NORET:
        case LLIL_IF:
            return true;
        default:
            return false;
        }
    };

    for (auto& block : blocks)
    {
        symbolicLifter.Reset();
        // The current symbolic VM stack is local to a basic block. Branch/join blocks
        // must stay on the old physical stack model until cross-edge VM stack-state
        // propagation exists. For large entry/setup blocks, allow symbolic lifting for
        // the straight-line prefix even if the block eventually ends in a branch; this
        // preserves the main-script static initialization cleanup. Small control-flow
        // blocks are usually YSC short-circuit boolean helpers and must remain physical.
        bool hasIncomingEdges = !block->GetIncomingEdges().empty();
        bool hasControlFlow = BlockContainsControlFlow(view, block, GetMaxInstructionLength());
        size_t rawInstructionCount = CountRawInstructions(view, block, GetMaxInstructionLength());
        bool allowSymbolicBlock = useFunctionLevelStackLifting || (!hasIncomingEdges && (!hasControlFlow || rawInstructionCount >= 32));
        function->SetCurrentSourceBlock(block);
        context.PrepareBlockTranslation(function, block->GetArchitecture(), block->GetStart());

        if (auto label = function->GetLabelForAddress(block->GetArchitecture(), block->GetStart()))
            function->MarkLabel(*label);

        if (useFunctionLevelStackLifting)
        {
            auto depthIt = vmInputDepths.find(block->GetStart());
            if (depthIt == vmInputDepths.end())
            {
                useFunctionLevelStackLifting = false;
                symbolicLifter.Reset();
            }
            else
            {
                symbolicLifter.SeedStack(vmBlockIndices[block->GetStart()], depthIt->second);
            }
        }

        size_t blockStartInstr = function->GetInstructionCount();

        for (uint64_t addr = block->GetStart(); addr < block->GetEnd();)
        {
            if (view->AnalysisIsAborted())
                return false;

            function->SetCurrentAddress(block->GetArchitecture(), addr);
            function->ClearIndirectBranches();

            size_t len = 0;
            const uint8_t* opcode = nullptr;
            std::vector<uint8_t> ownedOpcode;

            if (block->HasInstructionData())
            {
                opcode = block->GetInstructionData(addr, &len);
            }

            if (!opcode || len == 0)
            {
                ownedOpcode.resize(GetMaxInstructionLength());
                len = view->Read(ownedOpcode.data(), addr, ownedOpcode.size());
                opcode = ownedOpcode.data();
            }

            if (len == 0)
            {
                function->AddInstruction(function->Unimplemented());
                break;
            }

            size_t instrCountBefore = function->GetInstructionCount();
            bool status = false;
            size_t oldLen = len;
            if (allowSymbolicBlock && block->GetArchitecture() == this)
            {
                if (useFunctionLevelStackLifting)
                {
                    size_t instrLenForEffect = GetRawInstructionLength(view, addr, opcode, oldLen);
                    int delta = 0;
                    bool instrTerminal = false;
                    std::optional<size_t> expectedOutgoingDepth;
                    std::vector<uint32_t> expectedOutgoingBlockIndices;
                    bool branchExportsStack = opcode[0] == OP_J || opcode[0] == OP_JZ || opcode[0] == OP_IEQ_JZ ||
                        opcode[0] == OP_INE_JZ || opcode[0] == OP_IGT_JZ || opcode[0] == OP_IGE_JZ ||
                        opcode[0] == OP_ILT_JZ || opcode[0] == OP_ILE_JZ || opcode[0] == OP_SWITCH;
                    if (instrLenForEffect != 0 && GetYSCStackEffect(view, addr, opcode, instrLenForEffect, delta, instrTerminal) && branchExportsStack)
                    {
                        int outDepth = static_cast<int>(symbolicLifter.StackDepth()) + delta;
                        if (outDepth >= 0)
                        {
                            expectedOutgoingDepth = static_cast<size_t>(outDepth);
                            for (auto& edge : block->GetOutgoingEdges())
                            {
                                if (edge.target && vmBlockIndices.contains(edge.target->GetStart()))
                                    expectedOutgoingBlockIndices.push_back(vmBlockIndices[edge.target->GetStart()]);
                            }
                        }
                    }
                    bool isConditionalBranch = opcode[0] == OP_JZ || opcode[0] == OP_IEQ_JZ || opcode[0] == OP_INE_JZ ||
                        opcode[0] == OP_IGT_JZ || opcode[0] == OP_IGE_JZ || opcode[0] == OP_ILT_JZ || opcode[0] == OP_ILE_JZ;
                    if (expectedOutgoingDepth && isConditionalBranch)
                    {
                        std::optional<uint32_t> fallthroughBlockIndex;
                        std::optional<uint32_t> branchBlockIndex;
                        uint64_t branchTarget = BranchTarget(addr, opcode);
                        uint64_t fallthroughTarget = addr + instrLenForEffect;
                        for (auto& edge : block->GetOutgoingEdges())
                        {
                            if (!edge.target)
                                continue;
                            uint64_t target = edge.target->GetStart();
                            auto indexIt = vmBlockIndices.find(target);
                            if (indexIt == vmBlockIndices.end())
                                continue;
                            if (target == branchTarget)
                                branchBlockIndex = indexIt->second;
                            if (target == fallthroughTarget)
                                fallthroughBlockIndex = indexIt->second;
                        }
                        bool explicitFallthroughGoto = true;
                        auto nextBlockIt = nextBlockStarts.find(block->GetStart());
                        if (nextBlockIt != nextBlockStarts.end() && nextBlockIt->second == fallthroughTarget)
                            explicitFallthroughGoto = false;
                        symbolicLifter.SetExpectedBranchStackTargets(expectedOutgoingDepth, fallthroughBlockIndex, branchBlockIndex, explicitFallthroughGoto);
                    }
                    else
                    {
                        symbolicLifter.SetExpectedOutgoingStackDepth(expectedOutgoingDepth, expectedOutgoingBlockIndices);
                    }
                }
                status = symbolicLifter.Lift(opcode, addr, len);
            }

            if (!status)
            {
                if (useFunctionLevelStackLifting)
                {
                    std::string functionName = owner && owner->GetSymbol() ? owner->GetSymbol()->GetShortName() : "<unknown>";
                    BinaryNinja::LogInfo("YSC new-lift abort for %s @ %#llx: addr=%#llx opcode=%s reason=symbolic-lift-failed",
                                         functionName.c_str(), static_cast<unsigned long long>(functionStart),
                                         static_cast<unsigned long long>(addr), OpcodeName(opcode[0]));
                    return false;
                }
                symbolicLifter.DisableAndFlush();
                len = oldLen;
                status = block->GetArchitecture()->GetInstructionLowLevelIL(opcode, addr, len, *function);
            }
            size_t instrCountAfter = function->GetInstructionCount();

            if (!status || len == 0)
            {
                if (useFunctionLevelStackLifting)
                {
                    std::string functionName = owner && owner->GetSymbol() ? owner->GetSymbol()->GetShortName() : "<unknown>";
                    BinaryNinja::LogInfo("YSC new-lift abort for %s @ %#llx: addr=%#llx opcode=%s reason=bad-lift-result len=%zu status=%d",
                                         functionName.c_str(), static_cast<unsigned long long>(functionStart),
                                         static_cast<unsigned long long>(addr), OpcodeName(opcode[0]), len, status ? 1 : 0);
                    return false;
                }
                function->AddInstruction(function->Unimplemented());
                break;
            }

            context.CheckForInlinedCall(block, instrCountBefore, instrCountAfter, addr, addr + len, opcode, len, {});
            addr += len;
        }

        function->ClearIndirectBranches();

        bool blockHasInstructions = function->GetInstructionCount() != blockStartInstr;
        bool blockEndsTerminal = false;
        if (blockHasInstructions)
        {
            auto currentLastInstr = function->GetInstruction(function->GetInstructionCount() - 1);
            blockEndsTerminal = isTerminal(currentLastInstr);
        }

        if (useFunctionLevelStackLifting && !blockEndsTerminal && !block->GetOutgoingEdges().empty())
        {
            bool storedAnySuccessor = false;
            for (auto& edge : block->GetOutgoingEdges())
            {
                if (!edge.target)
                    continue;
                if (IsZeroReturnLeaveBlock(view, edge.target, GetMaxInstructionLength()))
                    continue;
                auto depthIt = vmInputDepths.find(edge.target->GetStart());
                auto indexIt = vmBlockIndices.find(edge.target->GetStart());
                if (depthIt == vmInputDepths.end() || indexIt == vmBlockIndices.end())
                    continue;
                if (!symbolicLifter.StoreStackOutputs(indexIt->second, depthIt->second))
                    return false;
                storedAnySuccessor = true;
            }
            if (!storedAnySuccessor && !blockHasInstructions)
                function->AddInstruction(function->Nop());
        }

        if (function->GetInstructionCount() == blockStartInstr)
        {
            if (useFunctionLevelStackLifting)
            {
                function->AddInstruction(function->Nop());
                goto ysc_block_nonempty;
            }
            function->AddInstruction(function->Unimplemented());
            continue;
        }

ysc_block_nonempty:

        auto lastInstr = function->GetInstruction(function->GetInstructionCount() - 1);
        if (isTerminal(lastInstr))
            continue;

        if ((block->GetOutgoingEdges().size() == 0) && !block->CanExit() && !block->IsFallThroughToFunction())
        {
            function->AddInstruction(function->NoReturn());
            continue;
        }

        if (auto exitLabel = function->GetLabelForAddress(block->GetArchitecture(), block->GetEnd()))
            function->AddInstruction(function->Goto(*exitLabel));
        else
            function->AddInstruction(function->Jump(function->ConstPointer(GetAddressSize(), block->GetEnd())));
    }

    if (function->GetInstructionCount() == 0)
        function->AddInstruction(function->Unimplemented());

    function->Finalize();
    return true;
}

void YSCArchitecture::FreeFunctionArchContext(YSCFunctionContext* context)
{
    delete context;
}

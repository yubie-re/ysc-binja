#include "inc.hpp"
#include "YSCArchitecture.hpp"
#include "Architecture/FunctionFacts.hpp"
#include "Common/Env.hpp"
#include "Instructions/InstructionRegistry.hpp"
#include "Instructions/OperationEnum.hpp"
#include "Instructions/SwitchCase.hpp"
#include "Lifting/LiftingSupport.hpp"
#include "Lifting/YSCSymbolicLifter.hpp"
#include "Profiling/YSCTrace.hpp"
#include "Common/Uint24.hpp"
#include "lowlevelilinstruction.h"
#include <cstdlib>
#include <cstdint>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>

namespace
{
constexpr uint32_t YSC_LOCAL_TEMP_BASE = 0x10000;
constexpr uint32_t YSC_STACK_TEMP_BASE = 0x20000;
constexpr uint32_t YSC_CALL_RESULT_TEMP_BASE = 0x80000;
constexpr uint64_t YSC_WHOLE_FUNCTION_LIFT_SIZE_LIMIT = 2048;
constexpr size_t YSC_STACK_ANALYSIS_WORK_LIMIT = 4096;
constexpr size_t YSC_FUNCTION_ANALYSIS_FANOUT_LIMIT = 2048;
constexpr size_t YSC_MAX_RUNTIME_ARRAY_OFFSET_ALIASES_PER_BASE = 16;
constexpr bool YSC_ENABLE_AUTO_FUNCTION_SIGNATURES = false;
constexpr bool YSC_ENABLE_RUNTIME_ARRAY_METADATA_BY_DEFAULT = true;

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

bool YSCEnvEnabled(const char* name, bool defaultValue = false)
{
    return YSCGetEnvEnabled(name, defaultValue);
}

bool YSCRuntimeArrayMetadataEnabled()
{
    return YSCEnvEnabled("YSC_BINJA_RUNTIME_ARRAY_METADATA", YSC_ENABLE_RUNTIME_ARRAY_METADATA_BY_DEFAULT);
}

size_t YSCEnvSize(const char* name, size_t defaultValue)
{
    return YSCGetEnvSize(name, defaultValue);
}

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

void DefineAutoInt32DataSymbol(BinaryNinja::BinaryView* view, uint64_t address, const std::string& name)
{
    if (!view)
        return;

    BinaryNinja::DataVariable existingVariable {};
    if (!view->GetDataVariableAtAddress(address, existingVariable))
        view->DefineDataVariable(address, VolatileInt32Type());

    if (view->GetSymbolByAddress(address))
        return;

    view->DefineAutoSymbol(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, name, address));
}

bool ExistingAutoSignatureMatches(BinaryNinja::Function* function, uint8_t paramCount, uint8_t retCount)
{
    if (!function)
        return false;

    auto type = function->GetType();
    if (!type)
        return false;

    auto params = type->GetParameters();
    if (params.size() != paramCount)
        return false;

    for (uint8_t i = 0; i < paramCount; i++)
    {
        if (params[i].defaultLocation)
            return false;
        if (params[i].location.type != RegisterVariableSourceType || params[i].location.storage != ArgReg(i))
            return false;
    }

    auto returnType = type->GetChildType();
    if (!returnType)
        return false;
    bool returnsVoid = returnType->GetClass() == VoidTypeClass;
    return (retCount == 0) == returnsVoid;
}

void ApplyYSCFunctionType(BinaryNinja::Function* function, BinaryNinja::BinaryView* view,
                          const std::optional<YSCEnterInfo>& enter, const std::optional<uint8_t>& analyzedReturnCount)
{
    if (!YSC_ENABLE_AUTO_FUNCTION_SIGNATURES)
        return;
    if (!function || !view || !enter)
        return;
    if (enter->m_paramCount > 16)
        return;

    using namespace BinaryNinja;
    uint8_t retCount = analyzedReturnCount.value_or(FindFirstLeaveReturnCount(view, function->GetStart()));
    if (ExistingAutoSignatureMatches(function, enter->m_paramCount, retCount))
        return;

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

    Ref<Type> returnType = retCount > 0 ? Type::IntegerType(4, true) : Type::VoidType();
    auto cc = function->GetArchitecture()->GetDefaultCallingConvention();
    function->SetAutoType(Type::FunctionType(returnType, cc, params, false, 0));
    function->SetAutoParameterVariables(Confidence<std::vector<Variable>>(paramVars, 255));
}

void ApplyYSCFunctionTypeAt(BinaryNinja::Function* function, BinaryNinja::BinaryView* view, uint64_t addr)
{
    auto enter = ReadEnterInfo(view, addr);
    if (!enter)
        return;
    ApplyYSCFunctionType(function, view, enter, FindFirstLeaveReturnCount(view, addr));
}

size_t GetRawInstructionLength(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* data, size_t len);
bool IsZeroReturnLeaveBlock(BinaryNinja::BinaryView* view, const BinaryNinja::Ref<BinaryNinja::BasicBlock>& block,
                            size_t maxInstructionLength);

uint64_t GetCodeEnd(BinaryNinja::BinaryView* view)
{
    if (!view)
        return 0;
    auto code = view->GetSectionByName("CODE");
    return code ? code->GetEnd() : view->GetEnd();
}

size_t GetSwitchInstructionLength(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* data, size_t len)
{
    if (!data || len < 2)
        return 0;

    size_t size = 2 + static_cast<size_t>(data[1]) * sizeof(SwitchCase);
    if (!view)
        return 2;

    uint64_t codeEnd = GetCodeEnd(view);
    if (addr + size <= addr || addr + size > codeEnd)
        return 0;

    return size;
}

uint64_t GetSwitchCaseTarget(uint64_t switchAddr, size_t index, const SwitchCase& switchCase)
{
    return static_cast<uint64_t>(static_cast<int64_t>(switchAddr) + switchCase.m_target +
                                 static_cast<int64_t>((index + 1) * sizeof(SwitchCase) + 2));
}

std::optional<YSCSwitchInfo> DecodeSwitchInfo(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* data,
                                              size_t len)
{
    if (!view || !data || len < 2 || data[0] != OP_SWITCH)
        return std::nullopt;

    size_t instrLen = GetSwitchInstructionLength(view, addr, data, len);
    if (instrLen == 0)
        return std::nullopt;

    uint8_t count = data[1];
    std::vector<SwitchCase> cases(count);
    size_t tableSize = cases.size() * sizeof(SwitchCase);
    if (tableSize > 0 && view->Read(cases.data(), addr + 2, tableSize) != tableSize)
        return std::nullopt;

    YSCSwitchInfo switchInfo;
    switchInfo.m_address = addr;
    switchInfo.m_caseCount = count;
    switchInfo.m_tableStart = addr + 2;
    switchInfo.m_tableEnd = addr + instrLen;
    switchInfo.m_cases.reserve(cases.size());

    for (size_t i = 0; i < cases.size(); i++)
        switchInfo.m_cases.push_back(YSCSwitchCaseInfo{cases[i].m_case, GetSwitchCaseTarget(addr, i, cases[i])});

    return switchInfo;
}

struct YSCCodeIndex
{
    uint64_t codeStart = 0;
    uint64_t codeEnd = 0;
    std::unordered_set<uint64_t> instructionStarts;
    std::set<uint64_t> functionStarts;
};

std::mutex g_codeIndexMutex;
std::mutex g_codeIndexBuildMutex;
std::unordered_map<uintptr_t, std::shared_ptr<YSCCodeIndex>> g_codeIndexCache;

std::shared_ptr<YSCCodeIndex> GetYSCCodeIndex(BinaryNinja::BinaryView* view, size_t maxInstructionLength)
{
    uintptr_t key = GetYSCViewCacheKey(view);
    if (key == 0 || IsYSCViewKeyRetired(key))
        return nullptr;

    {
        std::lock_guard<std::mutex> guard(g_codeIndexMutex);
        auto cached = g_codeIndexCache.find(key);
        if (cached != g_codeIndexCache.end())
            return cached->second;
    }

    std::lock_guard<std::mutex> buildGuard(g_codeIndexBuildMutex);
    {
        std::lock_guard<std::mutex> guard(g_codeIndexMutex);
        auto cached = g_codeIndexCache.find(key);
        if (cached != g_codeIndexCache.end())
            return cached->second;
    }

    auto index = std::make_shared<YSCCodeIndex>();
    auto code = view->GetSectionByName("CODE");
    if (!code)
        return index;

    YSC_TRACE_SCOPE("ysc.arch", "GetYSCCodeIndex");
    YSCTrace::InstantInt("ysc.arch", "codeIndexBuildViewKey", "key", static_cast<int64_t>(key));
    YSCTrace::InstantInt("ysc.arch", "codeIndexBuildWrapper", "address",
                         static_cast<int64_t>(reinterpret_cast<uintptr_t>(view)));
    uint64_t firstDecodeFailure = 0;
    index->codeStart = code->GetStart();
    index->codeEnd = code->GetEnd();

    std::vector<uint8_t> opcode(maxInstructionLength);
    for (uint64_t cursor = index->codeStart; cursor < index->codeEnd && !view->AnalysisIsAborted();)
    {
        size_t maxLen = static_cast<size_t>(std::min<uint64_t>(maxInstructionLength, index->codeEnd - cursor));
        size_t len = view->Read(opcode.data(), cursor, maxLen);
        if (len < 1)
            break;

        size_t instrLen = GetRawInstructionLength(view, cursor, opcode.data(), len);
        if (instrLen == 0 || instrLen > len || cursor + instrLen <= cursor || cursor + instrLen > index->codeEnd)
        {
            firstDecodeFailure = cursor;
            break;
        }

        index->instructionStarts.insert(cursor);
        if (opcode[0] == OP_ENTER)
            index->functionStarts.insert(cursor);
        cursor += instrLen;
    }

    YSCTrace::Counter("ysc.arch", "codeBytes", static_cast<int64_t>(index->codeEnd - index->codeStart));
    YSCTrace::Counter("ysc.arch", "instructionStarts", static_cast<int64_t>(index->instructionStarts.size()));
    YSCTrace::Counter("ysc.arch", "functionStarts", static_cast<int64_t>(index->functionStarts.size()));
    if (firstDecodeFailure != 0)
        YSCTrace::InstantInt("ysc.arch", "codeIndexDecodeFailure", "address", static_cast<int64_t>(firstDecodeFailure));
    YSCTrace::MemorySnapshot("codeIndex.end.rssKb");
    YSCTrace::FlushThrottled();

    {
        std::lock_guard<std::mutex> guard(g_codeIndexMutex);
        auto cached = g_codeIndexCache.find(key);
        if (cached != g_codeIndexCache.end())
            return cached->second;
        g_codeIndexCache[key] = index;
    }
    return index;
}

bool IsIndexedInstructionStart(const std::shared_ptr<YSCCodeIndex>& index, uint64_t addr)
{
    return index && addr >= index->codeStart && addr < index->codeEnd && index->instructionStarts.contains(addr);
}

bool IsIndexedFunctionStart(const std::shared_ptr<YSCCodeIndex>& index, uint64_t addr)
{
    return index && addr >= index->codeStart && addr < index->codeEnd && index->functionStarts.contains(addr);
}

bool YSCDiagnosticsEnabled()
{
    return YSCEnvEnabled("YSC_BINJA_DIAGNOSTICS");
}

bool YSCExtraInlinedCallChecksEnabled()
{
    return YSCEnvEnabled("YSC_BINJA_ENABLE_INLINED_CALL_CHECKS");
}

bool YSCTraceCallChecksEnabled()
{
    return YSCEnvEnabled("YSC_BINJA_TRACE_CALL_CHECKS");
}

bool YSCLargeScriptCallAnalysisEnabled()
{
    return YSCEnvEnabled("YSC_BINJA_ENABLE_LARGE_SCRIPT_CALL_ANALYSIS");
}

bool YSCLimitFunctionFanout(const std::shared_ptr<YSCCodeIndex>& index)
{
    if (!index || YSCLargeScriptCallAnalysisEnabled())
        return false;
    size_t limit = YSCEnvSize("YSC_BINJA_FUNCTION_ANALYSIS_LIMIT", YSC_FUNCTION_ANALYSIS_FANOUT_LIMIT);
    return limit != 0 && index->functionStarts.size() > limit;
}

bool IsIndexedCodeAddress(const std::shared_ptr<YSCCodeIndex>& index, uint64_t addr)
{
    return index && addr >= index->codeStart && addr < index->codeEnd;
}

bool RegisterIndexedEnterFunction(BinaryNinja::BinaryView* view, const std::shared_ptr<YSCCodeIndex>& index, uint64_t addr)
{
    if (!view || !IsIndexedCodeAddress(index, addr))
        return false;

    auto enter = ReadEnterInfo(view, addr);
    if (!enter || enter->m_paramCount > 16)
        return false;

    size_t enterLen = 5 + enter->m_nameLength;
    if (enterLen == 0 || addr + enterLen <= addr || addr + enterLen > index->codeEnd)
        return false;

    std::lock_guard<std::mutex> guard(g_codeIndexMutex);
    index->instructionStarts.insert(addr);
    index->functionStarts.insert(addr);
    return true;
}

std::unordered_set<uint64_t> DecodeFunctionInstructionStarts(BinaryNinja::BinaryView* view,
                                                             const std::shared_ptr<YSCCodeIndex>& index,
                                                             uint64_t functionStart, size_t maxInstructionLength)
{
    std::unordered_set<uint64_t> starts;
    if (!view || !IsIndexedCodeAddress(index, functionStart))
        return starts;

    std::vector<uint8_t> opcode(maxInstructionLength);
    for (uint64_t cursor = functionStart; cursor < index->codeEnd && !view->AnalysisIsAborted();)
    {
        size_t maxLen = static_cast<size_t>(std::min<uint64_t>(maxInstructionLength, index->codeEnd - cursor));
        size_t len = view->Read(opcode.data(), cursor, maxLen);
        if (len < 1)
            break;
        if (cursor != functionStart && opcode[0] == OP_ENTER)
            break;

        size_t instrLen = GetRawInstructionLength(view, cursor, opcode.data(), len);
        if (instrLen == 0 || instrLen > len || cursor + instrLen <= cursor || cursor + instrLen > index->codeEnd)
            break;

        starts.insert(cursor);
        cursor += instrLen;
    }

    if (!starts.empty())
    {
        std::lock_guard<std::mutex> guard(g_codeIndexMutex);
        index->instructionStarts.insert(starts.begin(), starts.end());
    }
    return starts;
}

bool IsValidFunctionInstructionStart(const std::unordered_set<uint64_t>& functionInstructionStarts,
                                     const std::shared_ptr<YSCCodeIndex>& index, uint64_t target,
                                     uint64_t functionStart)
{
    if (!functionInstructionStarts.contains(target))
        return false;
    return !IsIndexedFunctionStart(index, target) || target == functionStart;
}

bool IsValidIntraFunctionTarget(const std::shared_ptr<YSCCodeIndex>& index, uint64_t target, uint64_t functionStart)
{
    if (!IsIndexedInstructionStart(index, target))
        return false;
    return !IsIndexedFunctionStart(index, target) || target == functionStart;
}

bool IsValidCallTarget(BinaryNinja::BinaryView* view, uint64_t target, size_t maxInstructionLength)
{
    auto index = GetYSCCodeIndex(view, maxInstructionLength);
    return IsIndexedFunctionStart(index, target) || RegisterIndexedEnterFunction(view, index, target);
}

void AddBenignInstructionIL(BinaryNinja::LowLevelILFunction& il, uint8_t opcode)
{
    switch (opcode)
    {
    case OP_LEAVE:
        il.AddInstruction(il.Return(il.ConstPointer(4, 0)));
        break;
    case OP_THROW:
        il.AddInstruction(il.NoReturn());
        break;
    default:
        il.AddInstruction(il.Nop());
        break;
    }
}

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
    case OP_FMOD:
    case OP_FEQ:
    case OP_FNE:
    case OP_FGT:
    case OP_FGE:
    case OP_FLT:
    case OP_FLE:
    case OP_IOFFSET:
        delta = -1;
        return true;
    case OP_VADD:
    case OP_VSUB:
    case OP_VMUL:
    case OP_VDIV:
        delta = -3;
        return true;
    case OP_INEG:
    case OP_INOT:
    case OP_FNEG:
    case OP_I2F:
    case OP_F2I:
    case OP_VNEG:
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
    case OP_F2V:
        delta = 2;
        return true;
    case OP_CATCH:
        delta = 1;
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
    case OP_THROW:
        terminal = true;
        return true;
    case OP_CALLINDIRECT:
        delta = -1;
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
        if (!IsValidCallTarget(view, target, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH))
            return false;
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
    YSC_TRACE_SCOPE_I("ysc.arch", "AnalyzeFunctionVMStack", "blocks", blocks.size());
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
    case OP_FMOD:
    case OP_FNEG:
    case OP_FEQ:
    case OP_FNE:
    case OP_FGT:
    case OP_FGE:
    case OP_FLT:
    case OP_FLE:
    case OP_I2F:
    case OP_F2I:
    case OP_F2V:
    case OP_VADD:
    case OP_VSUB:
    case OP_VMUL:
    case OP_VDIV:
    case OP_VNEG:
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
    case OP_CATCH:
    case OP_THROW:
    case OP_CALLINDIRECT:
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
    YSC_TRACE_SCOPE_I("ysc.arch", "CanUseWholeFunctionStackLifting", "blocks", blocks.size());
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
        return GetSwitchInstructionLength(view, addr, data, len);
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

}

std::optional<YSCSwitchInfo> DecodeYSCSwitchInfo(BinaryNinja::BinaryView* view, uint64_t addr, const uint8_t* data,
                                                 size_t len)
{
    return DecodeSwitchInfo(view, addr, data, len);
}

bool IsYSCIndexedInstructionStart(BinaryNinja::BinaryView* view, uint64_t target, size_t maxInstructionLength)
{
    return IsIndexedInstructionStart(GetYSCCodeIndex(view, maxInstructionLength), target);
}

bool IsYSCValidCallTarget(BinaryNinja::BinaryView* view, uint64_t target, size_t maxInstructionLength)
{
    return IsValidCallTarget(view, target, maxInstructionLength);
}

YSCArchitecture::YSCArchitecture(const std::string& name) :
    BinaryNinja::ArchitectureWithFunctionContext<YSCFunctionContext>(name)
{}

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
    GetYSCInstructionInfo(insn, data + 1, addr, maxLen, result);
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
    GetYSCInstructionText(insn, data + 1, addr, len, result);
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

    switch (insn)
    {
    case OP_TEXT_LABEL_ASSIGN_STRING:
    case OP_TEXT_LABEL_ASSIGN_INT:
    case OP_TEXT_LABEL_APPEND_STRING:
    case OP_TEXT_LABEL_APPEND_INT:
    case OP_TEXT_LABEL_COPY:
    {
        size_t opLen = instrLen;
        bool success = GetYSCInstructionLowLevelIL(insn, data + 1, addr, opLen, il);
        len = instrLen;
        return success;
    }
    default:
        break;
    }

    AddBenignInstructionIL(il, insn);
    len = instrLen;
    return true;
}

/**
 * @brief Analyzes the basic blocks of a given function within the specified analysis context.
 */
void YSCArchitecture::AnalyzeBasicBlocks(BinaryNinja::Function* function, BinaryNinja::BasicBlockAnalysisContext& ctx)
{
    YSC_TRACE_SCOPE("ysc.arch", "AnalyzeBasicBlocks");
    YSCBlockAnalysisContext analysisCtx(function, &ctx);
    auto view = analysisCtx.GetView();
    if (!view || IsYSCArchitectureViewRetired(view) || view->AnalysisIsAborted())
    {
        YSCTrace::Instant("ysc.arch", "AnalyzeBasicBlocks skipped retired view");
        ctx.Finalize();
        return;
    }

    auto codeIndex = GetYSCCodeIndex(view, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH);
    bool limitFunctionFanout = YSCLimitFunctionFanout(codeIndex);

    if (!IsIndexedFunctionStart(codeIndex, function->GetStart()) &&
        !RegisterIndexedEnterFunction(view, codeIndex, function->GetStart()))
    {
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

    size_t invalidBlockCount = 0;
    size_t createdBlockCount = 0;
    size_t queuedEdgeCount = 0;
    uint64_t totalSize = 0;
    uint64_t maxSize = ctx.GetMaxFunctionSize();
    bool maxSizeReached = false;
    const uint64_t functionStart = function->GetStart();
    auto functionInstructionStarts = DecodeFunctionInstructionStarts(view, codeIndex, functionStart, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH);

    auto addBranchTarget = [&](BinaryNinja::Ref<BinaryNinja::BasicBlock>& block, BNBranchType type, uint64_t target,
                               bool fallThrough = false) -> bool {
        if (!IsValidFunctionInstructionStart(functionInstructionStarts, codeIndex, target, functionStart))
            return false;
        block->AddPendingOutgoingEdge(type, target, nullptr, fallThrough);
        analysisCtx.QueueAddress(target);
        queuedEdgeCount++;
        return true;
    };

    while (analysisCtx.IsProcessing())
    {
        if (IsYSCArchitectureViewRetired(view) || view->AnalysisIsAborted())
            break;

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
            if (currentAddr != blockStartAddr && IsIndexedFunctionStart(codeIndex, currentAddr))
                break;

            std::vector<uint8_t> opcode(YSC_MAX_INTERNAL_INSTRUCTION_LENGTH);
            size_t maxLen = view->Read(opcode.data(), currentAddr, opcode.size());
            if (maxLen < 1)
            {
                block->SetHasInvalidInstructions(true);
                invalidBlockCount++;
                break;
            }

            uint8_t insn = opcode[0];
            if (insn >= OP_MAX)
            {
                block->SetHasInvalidInstructions(true);
                invalidBlockCount++;
                break;
            }

            size_t instrLen = GetRawInstructionLength(view, currentAddr, opcode.data(), maxLen);
            if (instrLen == 0 || instrLen > maxLen)
            {
                block->SetHasInvalidInstructions(true);
                invalidBlockCount++;
                break;
            }

            if (!functionInstructionStarts.contains(currentAddr))
            {
                block->SetHasInvalidInstructions(true);
                invalidBlockCount++;
                break;
            }

            BinaryNinja::InstructionInfo info;
            bool endsBlock = false;
            switch (insn)
            {
            case OP_ENTER:
                if (auto enter = ReadEnterInfo(view, currentAddr))
                    analysisCtx.RecordEnter(*enter);
                break;
            case OP_CALL:
            {
                auto code = view ? view->GetSectionByName("CODE") : nullptr;
                if (!limitFunctionFanout && code && instrLen >= 4)
                {
                    uint64_t target = code->GetStart() + DecodeU24(opcode.data() + 1);
                    if (IsValidCallTarget(view, target, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH))
                    {
                        ctx.GetDirectCodeReferences()[target].emplace(function->GetArchitecture(), currentAddr);
                    }
                }
                break;
            }
            case OP_J:
            {
                uint64_t target = BranchTarget(currentAddr, opcode.data());
                if (!addBranchTarget(block, BNBranchType::UnconditionalBranch, target))
                {
                    block->SetHasInvalidInstructions(true);
                    invalidBlockCount++;
                }
                endsBlock = true;
                break;
            }
            case OP_JZ:
            case OP_IEQ_JZ:
            case OP_INE_JZ:
            case OP_IGT_JZ:
            case OP_IGE_JZ:
            case OP_ILT_JZ:
            case OP_ILE_JZ:
            {
                uint64_t fallthrough = currentAddr + instrLen;
                uint64_t target = BranchTarget(currentAddr, opcode.data());
                bool validFallthrough = addBranchTarget(block, BNBranchType::TrueBranch, fallthrough, true);
                bool validBranch = addBranchTarget(block, BNBranchType::FalseBranch, target);
                if (!validFallthrough || !validBranch)
                {
                    block->SetHasInvalidInstructions(true);
                    invalidBlockCount++;
                }
                endsBlock = true;
                break;
            }
            case OP_SWITCH:
            {
                auto decodedSwitch = DecodeSwitchInfo(view, currentAddr, opcode.data(), maxLen);
                if (!decodedSwitch)
                {
                    block->SetHasInvalidInstructions(true);
                    invalidBlockCount++;
                    endsBlock = true;
                    break;
                }

                YSCSwitchInfo switchInfo = *decodedSwitch;
                switchInfo.m_cases.clear();
                for (const auto& switchCase : decodedSwitch->m_cases)
                {
                    if (addBranchTarget(block, BNBranchType::IndirectBranch, switchCase.m_target))
                        switchInfo.m_cases.push_back(switchCase);
                    else
                    {
                        block->SetHasInvalidInstructions(true);
                        invalidBlockCount++;
                    }
                }

                if (!addBranchTarget(block, BNBranchType::IndirectBranch, switchInfo.m_tableEnd, true))
                {
                    block->SetHasInvalidInstructions(true);
                    invalidBlockCount++;
                }

                analysisCtx.RecordSwitch(switchInfo);
                endsBlock = true;
                break;
            }
            case OP_LEAVE:
                if (instrLen >= 3)
                    analysisCtx.RecordReturnCount(opcode[2]);
                endsBlock = true;
                break;
            case OP_THROW:
                endsBlock = true;
                break;
            default:
                if (GetInstructionInfo(opcode.data(), currentAddr, maxLen, info))
                {
                    for (size_t i = 0; i < info.branchCount; i++)
                    {
                        if (info.branchType[i] == BNBranchType::CallDestination)
                            continue;
                        if (!IsValidFunctionInstructionStart(functionInstructionStarts, codeIndex, info.branchTarget[i], functionStart))
                        {
                            block->SetHasInvalidInstructions(true);
                            invalidBlockCount++;
                        }
                    }
                }
                break;
            }

            size_t bytesRead = instrLen;
            size_t instructionDataLen = insn == OP_SWITCH ? std::min<size_t>(maxLen, 2) : instrLen;
            analysisCtx.AddCurrentInstructionData(opcode.data(), instructionDataLen);

            if (bytesRead == 0)
            {
                // If no bytes were read, something went wrong - end the block
                block->SetHasInvalidInstructions(true);
                invalidBlockCount++;
                break;
            }

            if (currentAddr + bytesRead < currentAddr || currentAddr + bytesRead > view->GetEnd()) // overflow check
            {
                block->SetHasInvalidInstructions(true);
                invalidBlockCount++;
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
                if (IsValidFunctionInstructionStart(functionInstructionStarts, codeIndex, currentAddr, functionStart))
                {
                    endsBlock = true;
                    block->AddPendingOutgoingEdge(BNBranchType::UnconditionalBranch, currentAddr, nullptr, true);
                }
            }

            if (endsBlock)
                break;
        }

        // Only add the block if it has instructions (currentAddr moved from start)
        if (currentAddr > blockStartAddr)
        {
            block->SetEnd(currentAddr);
            ctx.AddFunctionBasicBlock(block);
            createdBlockCount++;
        }

        analysisCtx.MarkBlockAsProcessed(blockStartAddr);

        if (maxSizeReached)
            break;
    }

    if (maxSizeReached)
        ctx.SetMaxSizeReached(true);

    ApplyYSCFunctionType(function, view, analysisCtx.GetFunctionContext()->m_enter,
                         analysisCtx.GetFunctionContext()->m_returnCount);
    (void) invalidBlockCount;
    YSCTrace::Counter("ysc.arch", "analyzeBlocks.createdBlocks", static_cast<int64_t>(createdBlockCount));
    YSCTrace::Counter("ysc.arch", "analyzeBlocks.queuedEdges", static_cast<int64_t>(queuedEdgeCount));
    YSCTrace::Counter("ysc.arch", "analyzeBlocks.invalidBlocks", static_cast<int64_t>(invalidBlockCount));
    YSCTrace::Counter("ysc.arch", "analyzeBlocks.totalBytes", static_cast<int64_t>(totalSize));
    YSCTrace::Counter("ysc.arch", "analyzeBlocks.functionInstructionStarts",
                      static_cast<int64_t>(functionInstructionStarts.size()));
    YSCTrace::Counter("ysc.arch", "analyzeBlocks.callFanoutLimited", limitFunctionFanout ? 1 : 0);
    YSCTrace::MemorySnapshot("analyzeBlocks.end.rssKb");
    YSCTrace::FlushThrottled();
    ctx.Finalize();
}

bool YSCArchitecture::LiftFunction(BinaryNinja::LowLevelILFunction* function,
                                   BinaryNinja::FunctionLifterContext& context)
{
    YSC_TRACE_SCOPE("ysc.arch", "LiftFunction");
    auto view = context.GetView();
    if (!view || IsYSCArchitectureViewRetired(view) || view->AnalysisIsAborted())
    {
        YSCTrace::Instant("ysc.arch", "LiftFunction skipped retired view");
        function->Finalize();
        return true;
    }

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
    auto liftCodeIndex = GetYSCCodeIndex(view, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH);
    if (!IsIndexedFunctionStart(liftCodeIndex, functionStart) &&
        !RegisterIndexedEnterFunction(view, liftCodeIndex, functionStart))
    {
        if (blocks.empty())
        {
            function->Finalize();
            return true;
        }

        auto block = blocks.front();
        function->SetCurrentSourceBlock(block);
        context.PrepareBlockTranslation(function, block->GetArchitecture(), block->GetStart());
        function->SetCurrentAddress(block->GetArchitecture(), block->GetStart());
        if (auto label = function->GetLabelForAddress(block->GetArchitecture(), block->GetStart()))
            function->MarkLabel(*label);
        function->AddInstruction(function->NoReturn());
        function->Finalize();
        return true;
    }
    std::unordered_set<uint64_t> functionInstructionStarts;
    bool limitFunctionFanout = YSCLimitFunctionFanout(liftCodeIndex);
    for (auto& block : blocks)
    {
        if (block->GetArchitecture() != this)
            continue;
        for (uint64_t addr = block->GetStart(); addr < block->GetEnd();)
        {
            std::vector<uint8_t> opcode(YSC_MAX_INTERNAL_INSTRUCTION_LENGTH);
            size_t len = view->Read(opcode.data(), addr, opcode.size());
            size_t instrLen = GetRawInstructionLength(view, addr, opcode.data(), len);
            if (len == 0 || instrLen == 0 || addr + instrLen <= addr)
                break;
            functionInstructionStarts.insert(addr);
            addr += instrLen;
        }
    }
    symbolicLifter.SetValidInstructionStarts(functionInstructionStarts);
    bool canWholeFunctionLift = CanUseWholeFunctionStackLifting(view, blocks, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH, &canDiagnostic);
    if (!startsWithEnter && canWholeFunctionLift)
    {
        canDiagnostic.reason = "non-enter-function";
        canDiagnostic.address = functionStart;
        canWholeFunctionLift = false;
    }
    bool stackAnalysisOk = canWholeFunctionLift &&
        AnalyzeFunctionVMStack(view, blocks, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH, vmInputDepths, functionStart, &stackDiagnostic);
    bool useFunctionLevelStackLifting = canWholeFunctionLift && stackAnalysisOk;
    YSCTrace::Counter("ysc.arch", "lift.blocks", static_cast<int64_t>(blocks.size()));
    YSCTrace::Counter("ysc.arch", "lift.useFunctionLevelStackLifting", useFunctionLevelStackLifting ? 1 : 0);
    if (YSCDiagnosticsEnabled() && useFunctionLevelStackLifting && blocks.size() >= 16)
    {
        std::string functionName = owner && owner->GetSymbol() ? owner->GetSymbol()->GetShortName() : "<unknown>";
        BinaryNinja::LogInfo("YSC new-lift decision for %s @ %#llx: canWhole=1 stackOk=1 use=1 blocks=%zu reason=ok addr=0 target=0 opcode=INVALID depth=0 otherDepth=0 totalSize=0 limit=%llu",
                             functionName.c_str(), static_cast<unsigned long long>(functionStart), blocks.size(),
                             static_cast<unsigned long long>(YSC_WHOLE_FUNCTION_LIFT_SIZE_LIMIT));
    }
    else if (YSCDiagnosticsEnabled() && !useFunctionLevelStackLifting && startsWithEnter)
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

    auto emitUnsupportedInstruction = [&](uint64_t addr, const uint8_t* opcode, size_t oldLen, size_t& len) {
        size_t instrLen = GetRawInstructionLength(view, addr, opcode, oldLen);
        if (instrLen == 0)
            instrLen = oldLen > 0 ? 1 : 0;
        AddBenignInstructionIL(*function, opcode && oldLen > 0 ? opcode[0] : OP_NOP);
        len = instrLen;
        return instrLen != 0;
    };

    size_t liftedInstructionCount = 0;
    size_t fallbackInstructionCount = 0;
    size_t directCallCount = 0;
    size_t nativeCallCount = 0;
    size_t indirectCallCount = 0;
    size_t inlinedCallCheckCount = 0;

    for (auto& block : blocks)
    {
        symbolicLifter.Reset();
        bool allowSymbolicBlock = block->GetArchitecture() == this;
        function->SetCurrentSourceBlock(block);
        context.PrepareBlockTranslation(function, block->GetArchitecture(), block->GetStart());
        function->SetCurrentAddress(block->GetArchitecture(), block->GetStart());

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
        bool blockSymbolicValid = true;

        for (uint64_t addr = block->GetStart(); addr < block->GetEnd();)
        {
            if (IsYSCArchitectureViewRetired(view) || view->AnalysisIsAborted())
            {
                function->Finalize();
                return true;
            }

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
                ownedOpcode.resize(YSC_MAX_INTERNAL_INSTRUCTION_LENGTH);
                len = view->Read(ownedOpcode.data(), addr, ownedOpcode.size());
                opcode = ownedOpcode.data();
            }

            if (len == 0)
            {
                function->AddInstruction(function->Nop());
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
            if (status)
                liftedInstructionCount++;

            if (!status)
            {
                blockSymbolicValid = false;
                fallbackInstructionCount++;
                symbolicLifter.Reset();
                int delta = 0;
                bool terminal = false;
                size_t instrLen = GetRawInstructionLength(view, addr, opcode, oldLen);
                if (instrLen != 0 && GetYSCStackEffect(view, addr, opcode, instrLen, delta, terminal))
                {
                    function->AddInstruction(terminal ? function->NoReturn() : function->Nop());
                    len = instrLen;
                    status = true;
                }
                else
                {
                    status = emitUnsupportedInstruction(addr, opcode, oldLen, len);
                }
            }
            size_t instrCountAfter = function->GetInstructionCount();

            if (!status || len == 0)
            {
                function->AddInstruction(function->Nop());
                break;
            }

            if (opcode[0] == OP_CALL)
                directCallCount++;
            else if (opcode[0] == OP_NATIVE)
                nativeCallCount++;
            else if (opcode[0] == OP_CALLINDIRECT)
                indirectCallCount++;

            bool checkInlinedCall = !limitFunctionFanout &&
                (opcode[0] == OP_CALL ||
                 (YSCExtraInlinedCallChecksEnabled() &&
                  (opcode[0] == OP_NATIVE || opcode[0] == OP_CALLINDIRECT)));
            if (checkInlinedCall)
            {
                if (YSCTraceCallChecksEnabled())
                {
                    YSC_TRACE_SCOPE("ysc.bn", "CheckForInlinedCall");
                    context.CheckForInlinedCall(block, instrCountBefore, instrCountAfter, addr, addr + len, opcode, len, {});
                }
                else
                {
                    context.CheckForInlinedCall(block, instrCountBefore, instrCountAfter, addr, addr + len, opcode, len, {});
                }
                inlinedCallCheckCount++;
            }
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

        if (useFunctionLevelStackLifting && blockSymbolicValid && !blockEndsTerminal && !block->GetOutgoingEdges().empty())
        {
            bool storedAnySuccessor = false;
            for (auto& edge : block->GetOutgoingEdges())
            {
                if (!edge.target)
                    continue;
                if (IsZeroReturnLeaveBlock(view, edge.target, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH))
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
            function->AddInstruction(function->Nop());
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

        if (functionInstructionStarts.contains(block->GetEnd()) || IsIndexedInstructionStart(liftCodeIndex, block->GetEnd()))
        {
            if (auto exitLabel = function->GetLabelForAddress(block->GetArchitecture(), block->GetEnd()))
                function->AddInstruction(function->Goto(*exitLabel));
            else
                function->AddInstruction(function->Jump(function->ConstPointer(GetAddressSize(), block->GetEnd())));
        }
        else
        {
            function->AddInstruction(function->NoReturn());
        }
    }

    if (function->GetInstructionCount() == 0 && !blocks.empty())
    {
        auto block = blocks.front();
        function->SetCurrentSourceBlock(block);
        context.PrepareBlockTranslation(function, block->GetArchitecture(), block->GetStart());
        function->SetCurrentAddress(block->GetArchitecture(), block->GetStart());
        if (auto label = function->GetLabelForAddress(block->GetArchitecture(), block->GetStart()))
            function->MarkLabel(*label);
        function->AddInstruction(function->Nop());
    }

    YSCTrace::Counter("ysc.arch", "lift.instructions", static_cast<int64_t>(liftedInstructionCount));
    YSCTrace::Counter("ysc.arch", "lift.fallbackInstructions", static_cast<int64_t>(fallbackInstructionCount));
    YSCTrace::Counter("ysc.arch", "lift.directCalls", static_cast<int64_t>(directCallCount));
    YSCTrace::Counter("ysc.arch", "lift.nativeCalls", static_cast<int64_t>(nativeCallCount));
    YSCTrace::Counter("ysc.arch", "lift.indirectCalls", static_cast<int64_t>(indirectCallCount));
    YSCTrace::Counter("ysc.arch", "lift.inlinedCallChecks", static_cast<int64_t>(inlinedCallCheckCount));
    YSCTrace::Counter("ysc.arch", "lift.callFanoutLimited", limitFunctionFanout ? 1 : 0);
    TraceYSCFunctionFactCounters(view);
    YSCTrace::MemorySnapshot("lift.end.rssKb");
    YSCTrace::FlushThrottled();
    function->Finalize();
    return true;
}

void YSCArchitecture::FreeFunctionArchContext(YSCFunctionContext* context)
{
    delete context;
}

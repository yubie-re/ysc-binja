#include "inc.hpp"
#include "YSCArchitecture.hpp"
#include "Instructions/OperationEnum.hpp"
#include "Instructions/SubOperations/OpSwitch.hpp"
#include "Profiling/YSCTrace.hpp"
#include "Uint24.hpp"
#include "lowlevelilinstruction.h"
#include <cstdlib>
#include <cstdint>
#include <map>
#include <mutex>
#include <queue>
#include <set>
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
constexpr bool YSC_ENABLE_RUNTIME_ARRAY_METADATA = false;

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
    const char* value = std::getenv(name);
    if (!value)
        return defaultValue;
    if (value[0] == '\0' || value[0] == '0')
        return false;
    if ((value[0] == 'f' || value[0] == 'F') && value[1] == '\0')
        return false;
    return true;
}

size_t YSCEnvSize(const char* name, size_t defaultValue)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
        return defaultValue;

    char* end = nullptr;
    unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value)
        return defaultValue;
    return static_cast<size_t>(parsed);
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

bool IsEnterAt(BinaryNinja::BinaryView* view, uint64_t addr)
{
    uint8_t op = 0;
    return view && view->Read(&op, addr, 1) == 1 && op == OP_ENTER;
}

std::optional<YSCEnterInfo> ReadEnterInfoRaw(BinaryNinja::BinaryView* view, uint64_t addr)
{
    uint8_t header[5] = {};
    if (!IsEnterAt(view, addr) || view->Read(header, addr, sizeof(header)) < sizeof(header))
        return std::nullopt;

    YSCEnterInfo enter;
    enter.m_address = addr;
    enter.m_paramCount = header[1];
    enter.m_localCount = ReadUnaligned<uint16_t>(header + 2);
    enter.m_nameLength = header[4];
    if (enter.m_nameLength > 0)
    {
        std::vector<char> name(enter.m_nameLength);
        if (view->Read(name.data(), addr + 5, name.size()) == name.size())
            enter.m_name.assign(name.data(), name.size());
    }
    return enter;
}

struct YSCFunctionFactCache
{
    std::unordered_map<uint64_t, std::optional<YSCEnterInfo>> enterInfo;
    std::unordered_map<uint64_t, uint8_t> returnCounts;
    size_t enterHits = 0;
    size_t enterMisses = 0;
    size_t returnHits = 0;
    size_t returnMisses = 0;
};

uintptr_t GetYSCViewCacheKey(BinaryNinja::BinaryView* view)
{
    if (!view)
        return 0;
    if (auto object = view->GetObject())
        return reinterpret_cast<uintptr_t>(object);
    return reinterpret_cast<uintptr_t>(view);
}

std::mutex g_functionFactMutex;
std::unordered_map<uintptr_t, std::shared_ptr<YSCFunctionFactCache>> g_functionFactCache;
std::mutex g_viewLifetimeMutex;
std::unordered_set<uintptr_t> g_retiredViewKeys;

bool IsYSCViewKeyRetired(uintptr_t key)
{
    if (key == 0)
        return true;
    std::lock_guard<std::mutex> guard(g_viewLifetimeMutex);
    return g_retiredViewKeys.contains(key);
}

bool IsYSCArchitectureViewRetired(BinaryNinja::BinaryView* view)
{
    return IsYSCViewKeyRetired(GetYSCViewCacheKey(view));
}

std::shared_ptr<YSCFunctionFactCache> GetYSCFunctionFactCache(BinaryNinja::BinaryView* view)
{
    uintptr_t key = GetYSCViewCacheKey(view);
    if (key == 0 || IsYSCViewKeyRetired(key))
        return nullptr;

    std::lock_guard<std::mutex> guard(g_functionFactMutex);
    auto it = g_functionFactCache.find(key);
    if (it != g_functionFactCache.end())
        return it->second;

    auto cache = std::make_shared<YSCFunctionFactCache>();
    g_functionFactCache[key] = cache;
    return cache;
}

std::optional<YSCEnterInfo> ReadEnterInfo(BinaryNinja::BinaryView* view, uint64_t addr)
{
    auto cache = GetYSCFunctionFactCache(view);
    if (!cache)
        return std::nullopt;

    {
        std::lock_guard<std::mutex> guard(g_functionFactMutex);
        auto cached = cache->enterInfo.find(addr);
        if (cached != cache->enterInfo.end())
        {
            cache->enterHits++;
            return cached->second;
        }
        cache->enterMisses++;
    }

    auto result = ReadEnterInfoRaw(view, addr);
    {
        std::lock_guard<std::mutex> guard(g_functionFactMutex);
        cache->enterInfo[addr] = result;
    }
    return result;
}

uint8_t GetEnterParamCount(BinaryNinja::BinaryView* view, uint64_t addr)
{
    auto enter = ReadEnterInfo(view, addr);
    return enter ? enter->m_paramCount : 0;
}

uint8_t FindFirstLeaveReturnCountRaw(BinaryNinja::BinaryView* view, uint64_t addr)
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
        if (cursor + size <= cursor || cursor + size > codeEnd)
            return 0;
        cursor += size;
    }
    return 0;
}

uint8_t FindFirstLeaveReturnCount(BinaryNinja::BinaryView* view, uint64_t addr)
{
    auto cache = GetYSCFunctionFactCache(view);
    if (!cache)
        return 0;

    {
        std::lock_guard<std::mutex> guard(g_functionFactMutex);
        auto cached = cache->returnCounts.find(addr);
        if (cached != cache->returnCounts.end())
        {
            cache->returnHits++;
            return cached->second;
        }
        cache->returnMisses++;
    }

    uint8_t result = FindFirstLeaveReturnCountRaw(view, addr);
    {
        std::lock_guard<std::mutex> guard(g_functionFactMutex);
        cache->returnCounts[addr] = result;
    }
    return result;
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
        case OP_FMOD: return Fmod(len);
        case OP_FNEG: return Unary(len, &BinaryNinja::LowLevelILFunction::FloatNeg);
        case OP_FEQ: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareEqual);
        case OP_FNE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareNotEqual);
        case OP_FGT: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareGreaterThan);
        case OP_FGE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareGreaterEqual);
        case OP_FLT: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareLessThan);
        case OP_FLE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareLessEqual);
        case OP_I2F: return Unary(len, &BinaryNinja::LowLevelILFunction::IntToFloat);
        case OP_F2I: return Unary(len, &BinaryNinja::LowLevelILFunction::FloatToInt);
        case OP_F2V: return F2V(len);
        case OP_VADD: return VectorBinary(len, &BinaryNinja::LowLevelILFunction::FloatAdd);
        case OP_VSUB: return VectorBinary(len, &BinaryNinja::LowLevelILFunction::FloatSub);
        case OP_VMUL: return VectorBinary(len, &BinaryNinja::LowLevelILFunction::FloatMult);
        case OP_VDIV: return VectorBinary(len, &BinaryNinja::LowLevelILFunction::FloatDiv);
        case OP_VNEG: return VectorUnary(len, &BinaryNinja::LowLevelILFunction::FloatNeg);
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
            if (len < 2) return false;
            return TextLabelAssignString(data[0], len);
        case OP_TEXT_LABEL_ASSIGN_INT:
            if (len < 2) return false;
            return TextLabelAssignInt(data[0], len);
        case OP_TEXT_LABEL_APPEND_STRING:
            if (len < 2) return false;
            return TextLabelAppendString(data[0], len);
        case OP_TEXT_LABEL_APPEND_INT:
            if (len < 2) return false;
            return TextLabelAppendInt(data[0], len);
        case OP_TEXT_LABEL_COPY:
            return TextLabelCopy(len);
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
        case OP_CATCH: Push(m_il.Const(4, static_cast<uint32_t>(-1)), static_cast<uint32_t>(-1)); len = 1; return true;
        case OP_THROW: m_il.AddInstruction(m_il.NoReturn()); len = 1; return true;
        case OP_CALLINDIRECT: return CallIndirect(len);
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
        m_syntheticInputCount = 0;
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
        m_syntheticInputCount = static_cast<uint32_t>(depth);
        for (size_t i = 0; i < depth; i++)
            Push(m_il.Register(4, StackTemp(blockIndex, static_cast<uint32_t>(i))));
    }

    void SetValidInstructionStarts(std::unordered_set<uint64_t> instructionStarts)
    {
        m_validInstructionStarts = std::move(instructionStarts);
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
        {
            value = Value{m_il.Register(4, StackTemp(m_currentBlockIndex, m_syntheticInputCount++)), Kind::Expr, 0, 0, std::nullopt, std::nullopt};
            return true;
        }
        value = m_stack.back();
        m_stack.pop_back();
        return true;
    }

    bool Dup(size_t& len)
    {
        if (m_stack.empty())
        {
            Value value;
            Pop(value);
            m_stack.push_back(value);
        }
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
    bool Fmod(size_t& len)
    {
        Value divisor, dividend;
        if (!Pop(divisor) || !Pop(dividend)) return false;
        auto quotient = m_il.FloatDiv(4, dividend.expr, divisor.expr);
        auto truncatedQuotient = m_il.FloatToInt(4, quotient);
        auto floatTruncatedQuotient = m_il.IntToFloat(4, truncatedQuotient);
        auto product = m_il.FloatMult(4, floatTruncatedQuotient, divisor.expr);
        Push(m_il.FloatSub(4, dividend.expr, product));
        len = 1;
        return true;
    }
    bool F2V(size_t& len)
    {
        Value value;
        if (!Pop(value)) return false;
        Push(value.expr);
        Push(value.expr);
        Push(value.expr);
        len = 1;
        return true;
    }
    bool VectorBinary(size_t& len, BinaryOp op)
    {
        Value z1, y1, x1, z2, y2, x2;
        if (!Pop(z1) || !Pop(y1) || !Pop(x1) || !Pop(z2) || !Pop(y2) || !Pop(x2)) return false;
        Push((m_il.*op)(4, x2.expr, x1.expr, 0, {}));
        Push((m_il.*op)(4, y2.expr, y1.expr, 0, {}));
        Push((m_il.*op)(4, z2.expr, z1.expr, 0, {}));
        len = 1;
        return true;
    }
    bool VectorUnary(size_t& len, UnaryOp op)
    {
        Value z, y, x;
        if (!Pop(z) || !Pop(y) || !Pop(x)) return false;
        Push((m_il.*op)(4, x.expr, 0, {}));
        Push((m_il.*op)(4, y.expr, 0, {}));
        Push((m_il.*op)(4, z.expr, 0, {}));
        len = 1;
        return true;
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
    bool EmitTextLabelIntrinsic(uint32_t intrinsic, std::vector<BinaryNinja::ExprId> params, size_t& len, size_t insnLen)
    {
        m_il.AddInstruction(m_il.Intrinsic({}, intrinsic, params));
        len = insnLen;
        return true;
    }
    bool TextLabelAssignString(uint8_t size, size_t& len)
    {
        Value dst, src;
        if (!Pop(dst) || !Pop(src)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelAssignString, {dst.expr, src.expr, m_il.Const(4, size)}, len, 2);
    }
    bool TextLabelAssignInt(uint8_t size, size_t& len)
    {
        Value dst, value;
        if (!Pop(dst) || !Pop(value)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelAssignInt, {dst.expr, value.expr, m_il.Const(4, size)}, len, 2);
    }
    bool TextLabelAppendString(uint8_t size, size_t& len)
    {
        Value dst, src;
        if (!Pop(dst) || !Pop(src)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelAppendString, {dst.expr, src.expr, m_il.Const(4, size)}, len, 2);
    }
    bool TextLabelAppendInt(uint8_t size, size_t& len)
    {
        Value dst, value;
        if (!Pop(dst) || !Pop(value)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelAppendInt, {dst.expr, value.expr, m_il.Const(4, size)}, len, 2);
    }
    bool TextLabelCopy(size_t& len)
    {
        Value dst, repeat, src;
        if (!Pop(dst) || !Pop(repeat) || !Pop(src)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelCopy, {dst.expr, repeat.expr, src.expr}, len, 1);
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
        if (!IsValidBranchTarget(BranchTarget(addr, opcode))) return false;
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;
        EmitGoto(BranchTarget(addr, opcode)); len = 3; return true;
    }
    bool Jz(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        Value cond; if (len < 3 || !Pop(cond)) return false;
        uint64_t branchTarget = BranchTarget(addr, opcode);
        uint64_t fallthroughTarget = addr + 3;
        if (!IsValidBranchTarget(branchTarget) || !IsValidBranchTarget(fallthroughTarget)) return false;
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
        if (!IsValidBranchTarget(branchTarget) || !IsValidBranchTarget(fallthroughTarget)) return false;
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

        auto decodedSwitch = DecodeSwitchInfo(m_view, addr, opcode, len);
        if (!decodedSwitch)
            return false;

        for (const auto& switchCase : decodedSwitch->m_cases)
            if (!IsValidBranchTarget(switchCase.m_target))
                return false;
        if (!IsValidBranchTarget(decodedSwitch->m_tableEnd))
            return false;

        std::vector<BinaryNinja::ArchAndAddr> indirectBranches;
        indirectBranches.reserve(decodedSwitch->m_cases.size() + 1);
        for (const auto& switchCase : decodedSwitch->m_cases)
            indirectBranches.emplace_back(m_arch, switchCase.m_target);
        indirectBranches.emplace_back(m_arch, decodedSwitch->m_tableEnd);
        m_il.SetIndirectBranches(indirectBranches);

        std::vector<BinaryNinja::LowLevelILLabel> falseLabels(decodedSwitch->m_cases.size());
        for (size_t i = 0; i < decodedSwitch->m_cases.size(); i++)
        {
            const auto& switchCase = decodedSwitch->m_cases[i];
            BinaryNinja::LowLevelILLabel trueLabel;
            m_il.AddInstruction(m_il.If(m_il.CompareEqual(4, selector.expr, m_il.Const(4, switchCase.m_case)), trueLabel, falseLabels[i]));
            m_il.MarkLabel(trueLabel);
            EmitGoto(switchCase.m_target);
            m_il.MarkLabel(falseLabels[i]);
        }
        EmitGoto(decodedSwitch->m_tableEnd);
        len = decodedSwitch->m_tableEnd - addr;
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
        if (!IsValidCallTarget(m_view, target, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH)) return false;
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
    bool CallIndirect(size_t& len)
    {
        Value target;
        if (!Pop(target)) return false;
        m_il.AddInstruction(m_il.Call(target.expr));
        len = 1;
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
    bool IsValidBranchTarget(uint64_t target)
    {
        if (!m_validInstructionStarts.empty())
            return m_validInstructionStarts.contains(target);
        return IsIndexedInstructionStart(GetYSCCodeIndex(m_view, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH), target);
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
        uint64_t address = (section ? section->GetStart() : 0) + operand * 4;
        if (section && m_view && address >= section->GetStart() && address < section->GetEnd())
            DefineAutoInt32DataSymbol(m_view, address, fmt::format("Local_{}", operand));
        return address;
    }
    uint64_t GlobalAddress(uint32_t operand)
    {
        auto section = m_view ? m_view->GetSectionByName("GLOBALS") : nullptr;
        uint32_t block = operand >> 18;
        uint32_t needle = operand & 0x3ffff;
        uint64_t address = (section ? section->GetStart() : 0) + (static_cast<uint64_t>(block) * (1 << 18) + needle) * 4;
        if (m_view)
            DefineAutoInt32DataSymbol(m_view, address, fmt::format("Global_{}", operand));
        return address;
    }

    void DefineRuntimeDataAddress(uint64_t address)
    {
        if (!YSC_ENABLE_RUNTIME_ARRAY_METADATA || !m_view)
            return;
        auto globals = m_view->GetSectionByName("GLOBALS");
        if (globals && address >= globals->GetStart() && address < globals->GetEnd())
        {
            uint64_t index = (address - globals->GetStart()) / 4;
            DefineAutoInt32DataSymbol(m_view, address, fmt::format("Global_{}", index));
        }
    }

    inline static std::mutex g_runtimeArrayShapeMutex;
    inline static std::set<uint64_t> g_definedRuntimeArrayBases;
    inline static std::set<uint64_t> g_definedRuntimeArrayOffsetAliases;
    inline static std::map<uint64_t, size_t> g_runtimeArrayOffsetAliasCounts;

    uint64_t RuntimeArrayKey(uint64_t dataAddress, uint32_t stride) const
    {
        return (dataAddress << 16) ^ stride;
    }

    void DefineRuntimeArrayDataAddress(uint64_t headerAddress, uint64_t dataAddress, uint32_t stride)
    {
        if (!YSC_ENABLE_RUNTIME_ARRAY_METADATA)
            return;
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
        DefineAutoInt32DataSymbol(m_view, dataAddress, fmt::format("{}_{}_data", prefix, headerIndex));
    }

    void DefineRuntimeArrayOffsetAlias(uint64_t dataAddress, int64_t offsetBytes)
    {
        if (!YSC_ENABLE_RUNTIME_ARRAY_METADATA || !m_view || offsetBytes < 0 || offsetBytes > 0x100)
            return;
        uint64_t aliasAddress = dataAddress + static_cast<uint64_t>(offsetBytes);

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

        std::lock_guard<std::mutex> guard(g_runtimeArrayShapeMutex);
        auto& aliasCount = g_runtimeArrayOffsetAliasCounts[dataAddress];
        if (aliasCount >= YSC_MAX_RUNTIME_ARRAY_OFFSET_ALIASES_PER_BASE)
            return;
        if (!g_definedRuntimeArrayOffsetAliases.insert(aliasAddress).second)
            return;
        aliasCount++;
        uint64_t dataIndex = (dataAddress - section->GetStart()) / 4;
        uint64_t headerIndex = dataIndex > 0 ? dataIndex - 1 : dataIndex;
        DefineAutoInt32DataSymbol(m_view, aliasAddress,
            fmt::format("{}_{}_data_plus_{:x}", prefix, headerIndex, static_cast<uint64_t>(offsetBytes)));
    }

    YSCArchitecture* m_arch;
    BinaryNinja::LowLevelILFunction& m_il;
    BinaryNinja::BinaryView* m_view;
    std::vector<Value> m_stack;
    bool m_symbolic = true;
    uint32_t m_currentBlockIndex = 0;
    size_t m_seededStackDepth = 0;
    uint32_t m_syntheticInputCount = 0;
    std::unordered_set<uint64_t> m_validInstructionStarts;
    std::optional<size_t> m_expectedOutgoingStackDepth;
    std::vector<uint32_t> m_expectedOutgoingBlockIndices;
    std::optional<uint32_t> m_expectedFallthroughBlockIndex;
    std::optional<uint32_t> m_expectedBranchBlockIndex;
    bool m_explicitFallthroughGoto = false;
};
}

bool EmitYSCTextLabelFallbackLLIL(uint8_t opcode, const uint8_t* data, size_t& len,
                                  BinaryNinja::LowLevelILFunction& il)
{
    auto emit = [&](uint32_t intrinsic, std::vector<BinaryNinja::ExprId> params, size_t instrLen) {
        il.AddInstruction(il.Intrinsic({}, intrinsic, params));
        len = instrLen;
        return true;
    };

    switch (opcode)
    {
    case OP_TEXT_LABEL_ASSIGN_STRING:
        if (!data || len < 2) return false;
        return emit(Intrin_TextLabelAssignString, {il.Pop(4), il.Pop(4), il.Const(4, data[0])}, 2);
    case OP_TEXT_LABEL_ASSIGN_INT:
        if (!data || len < 2) return false;
        return emit(Intrin_TextLabelAssignInt, {il.Pop(4), il.Pop(4), il.Const(4, data[0])}, 2);
    case OP_TEXT_LABEL_APPEND_STRING:
        if (!data || len < 2) return false;
        return emit(Intrin_TextLabelAppendString, {il.Pop(4), il.Pop(4), il.Const(4, data[0])}, 2);
    case OP_TEXT_LABEL_APPEND_INT:
        if (!data || len < 2) return false;
        return emit(Intrin_TextLabelAppendInt, {il.Pop(4), il.Pop(4), il.Const(4, data[0])}, 2);
    case OP_TEXT_LABEL_COPY:
        if (len < 1) return false;
        return emit(Intrin_TextLabelCopy, {il.Pop(4), il.Pop(4), il.Pop(4)}, 1);
    default:
        return false;
    }
}

uintptr_t MarkYSCArchitectureViewActive(BinaryNinja::BinaryView* view)
{
    uintptr_t key = GetYSCViewCacheKey(view);
    if (key == 0)
        return 0;

    {
        std::lock_guard<std::mutex> guard(g_viewLifetimeMutex);
        g_retiredViewKeys.erase(key);
    }
    YSCTrace::InstantInt("ysc.view", "YSC view active", "key", static_cast<int64_t>(key));
    return key;
}

void RetireYSCArchitectureView(uintptr_t key)
{
    if (key == 0)
        return;

    {
        std::lock_guard<std::mutex> guard(g_viewLifetimeMutex);
        g_retiredViewKeys.insert(key);
    }
    YSCTrace::InstantInt("ysc.view", "YSC view retired", "key", static_cast<int64_t>(key));
    YSCTrace::FlushThrottled();
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

    switch (insn)
    {
    case OP_TEXT_LABEL_ASSIGN_STRING:
    case OP_TEXT_LABEL_ASSIGN_INT:
    case OP_TEXT_LABEL_APPEND_STRING:
    case OP_TEXT_LABEL_APPEND_INT:
    case OP_TEXT_LABEL_COPY:
    {
        size_t opLen = instrLen;
        bool success = m_insns[insn]->GetInstructionLowLevelIL(data + 1, addr, opLen, il);
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
    auto charPtr = Type::PointerType(this, Type::IntegerType(1, true, "char"), false);
    auto constCharPtr = Type::PointerType(this, Type::IntegerType(1, true, "char"), true);
    auto constVoidPtr = Type::PointerType(this, Type::VoidType(), true);
    auto int32 = Type::IntegerType(4, true);
    auto uint32 = Type::IntegerType(4, false);
    switch (intrinsic)
    {
    case Intrin_StringHash:
        result.emplace_back("str", Type::PointerType(4, Type::IntegerType(1, false)));
        break;
    case Intrin_TextLabelAssignString:
    case Intrin_TextLabelAppendString:
        result.emplace_back("dst", charPtr);
        result.emplace_back("src", constCharPtr);
        result.emplace_back("size", uint32);
        break;
    case Intrin_TextLabelAssignInt:
    case Intrin_TextLabelAppendInt:
        result.emplace_back("dst", charPtr);
        result.emplace_back("value", int32);
        result.emplace_back("size", uint32);
        break;
    case Intrin_TextLabelCopy:
        result.emplace_back("dst", charPtr);
        result.emplace_back("repeat", uint32);
        result.emplace_back("src", constVoidPtr);
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
    case Intrin_TextLabelAssignString:
    case Intrin_TextLabelAssignInt:
    case Intrin_TextLabelAppendString:
    case Intrin_TextLabelAppendInt:
    case Intrin_TextLabelCopy:
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
    m_functionContext = std::make_unique<YSCFunctionContext>();
    m_functionContext->m_start = function->GetStart();
    m_blocksToProcess.push(function->GetStart());
    m_processingBlocks.insert(function->GetStart());
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
    if (auto cache = GetYSCFunctionFactCache(view))
    {
        std::lock_guard<std::mutex> guard(g_functionFactMutex);
        YSCTrace::Counter("ysc.arch", "functionFacts.enterHits", static_cast<int64_t>(cache->enterHits));
        YSCTrace::Counter("ysc.arch", "functionFacts.enterMisses", static_cast<int64_t>(cache->enterMisses));
        YSCTrace::Counter("ysc.arch", "functionFacts.returnHits", static_cast<int64_t>(cache->returnHits));
        YSCTrace::Counter("ysc.arch", "functionFacts.returnMisses", static_cast<int64_t>(cache->returnMisses));
    }
    YSCTrace::MemorySnapshot("lift.end.rssKb");
    YSCTrace::FlushThrottled();
    function->Finalize();
    return true;
}

void YSCArchitecture::FreeFunctionArchContext(YSCFunctionContext* context)
{
    delete context;
}

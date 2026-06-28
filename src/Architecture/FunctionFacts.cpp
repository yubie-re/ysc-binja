#include "inc.hpp"
#include "Architecture/FunctionFacts.hpp"
#include "Instructions/OperationEnum.hpp"
#include "Instructions/SwitchCase.hpp"
#include "Profiling/YSCTrace.hpp"
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
template <typename T>
T ReadUnalignedFact(const uint8_t* data)
{
    T result {};
    std::memcpy(&result, data, sizeof(T));
    return result;
}

std::optional<YSCEnterInfo> ReadEnterInfoRaw(BinaryNinja::BinaryView* view, uint64_t addr)
{
    uint8_t header[5] = {};
    if (!IsEnterAt(view, addr) || view->Read(header, addr, sizeof(header)) < sizeof(header))
        return std::nullopt;

    YSCEnterInfo enter;
    enter.m_address = addr;
    enter.m_paramCount = header[1];
    enter.m_localCount = ReadUnalignedFact<uint16_t>(header + 2);
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

std::mutex g_functionFactMutex;
std::unordered_map<uintptr_t, std::shared_ptr<YSCFunctionFactCache>> g_functionFactCache;
std::mutex g_viewLifetimeMutex;
std::unordered_set<uintptr_t> g_retiredViewKeys;

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
}

bool IsEnterAt(BinaryNinja::BinaryView* view, uint64_t addr)
{
    uint8_t op = 0;
    return view && view->Read(&op, addr, 1) == 1 && op == OP_ENTER;
}

uintptr_t GetYSCViewCacheKey(BinaryNinja::BinaryView* view)
{
    if (!view)
        return 0;
    if (auto object = view->GetObject())
        return reinterpret_cast<uintptr_t>(object);
    return reinterpret_cast<uintptr_t>(view);
}

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

void TraceYSCFunctionFactCounters(BinaryNinja::BinaryView* view)
{
    if (auto cache = GetYSCFunctionFactCache(view))
    {
        std::lock_guard<std::mutex> guard(g_functionFactMutex);
        YSCTrace::Counter("ysc.arch", "functionFacts.enterHits", static_cast<int64_t>(cache->enterHits));
        YSCTrace::Counter("ysc.arch", "functionFacts.enterMisses", static_cast<int64_t>(cache->enterMisses));
        YSCTrace::Counter("ysc.arch", "functionFacts.returnHits", static_cast<int64_t>(cache->returnHits));
        YSCTrace::Counter("ysc.arch", "functionFacts.returnMisses", static_cast<int64_t>(cache->returnMisses));
    }
}

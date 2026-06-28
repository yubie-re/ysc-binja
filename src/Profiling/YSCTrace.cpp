#include "YSCTrace.hpp"

#include "minitrace.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace
{
std::once_flag g_initOnce;
bool g_enabled = false;
std::mutex g_traceMutex;
std::chrono::steady_clock::time_point g_lastFlush;

int64_t CurrentRssKb()
{
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    long pages = 0;
    long resident = 0;
    if (!(statm >> pages >> resident))
        return 0;
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        return 0;
    return static_cast<int64_t>(resident) * pageSize / 1024;
#else
    return 0;
#endif
}

void ShutdownTrace()
{
    if (!g_enabled)
        return;
    std::lock_guard<std::mutex> guard(g_traceMutex);
    mtr_flush();
    mtr_shutdown();
    g_enabled = false;
}

void InitializeTrace()
{
    const char* tracePath = std::getenv("YSC_BINJA_TRACE");
    if (!tracePath || tracePath[0] == '\0')
        return;

    FILE* stream = std::fopen(tracePath, "wb");
    if (!stream)
        return;

    mtr_init_from_stream(stream);
    mtr_start();
    MTR_META_PROCESS_NAME("Binary Ninja YSC");
    g_enabled = true;
    g_lastFlush = std::chrono::steady_clock::now();
    std::atexit(ShutdownTrace);
}
}

namespace YSCTrace
{
bool Enabled()
{
    std::call_once(g_initOnce, InitializeTrace);
    return g_enabled;
}

void Flush()
{
    if (Enabled())
    {
        std::lock_guard<std::mutex> guard(g_traceMutex);
        mtr_flush();
        g_lastFlush = std::chrono::steady_clock::now();
    }
}

void FlushThrottled()
{
    if (!Enabled())
        return;
    auto now = std::chrono::steady_clock::now();
    if (now - g_lastFlush < std::chrono::milliseconds(1000))
        return;
    std::lock_guard<std::mutex> guard(g_traceMutex);
    now = std::chrono::steady_clock::now();
    if (now - g_lastFlush < std::chrono::milliseconds(1000))
        return;
    mtr_flush();
    g_lastFlush = now;
}

void Counter(const char* category, const char* name, int64_t value)
{
    if (Enabled())
        MTR_COUNTER(category, name, value);
}

void Instant(const char* category, const char* name)
{
    if (Enabled())
        MTR_INSTANT(category, name);
}

void InstantInt(const char* category, const char* name, const char* argName, int64_t value)
{
    if (Enabled())
        MTR_INSTANT_I(category, name, argName, value);
}

void MemorySnapshot(const char* name)
{
    if (Enabled())
        MTR_COUNTER("ysc.memory", name, CurrentRssKb());
}

Scope::Scope(const char* category, const char* name) : m_category(category), m_name(name), m_enabled(Enabled())
{
    if (m_enabled)
        MTR_BEGIN(m_category, m_name);
}

Scope::~Scope()
{
    if (m_enabled)
        MTR_END(m_category, m_name);
}

ScopeInt::ScopeInt(const char* category, const char* name, const char* argName, int64_t value) :
    m_category(category), m_name(name), m_argName(argName), m_value(value), m_enabled(Enabled())
{
    if (m_enabled)
        MTR_BEGIN_I(m_category, m_name, m_argName, m_value);
}

ScopeInt::~ScopeInt()
{
    if (m_enabled)
        MTR_END_I(m_category, m_name, m_argName, m_value);
}
}

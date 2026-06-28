#ifndef YSC_TRACE_HPP
#define YSC_TRACE_HPP

#include <cstdint>

namespace YSCTrace
{
bool Enabled();
void Flush();
void FlushThrottled();
void Counter(const char* category, const char* name, int64_t value);
void Instant(const char* category, const char* name);
void InstantInt(const char* category, const char* name, const char* argName, int64_t value);
void MemorySnapshot(const char* name);

class Scope
{
  public:
    Scope(const char* category, const char* name);
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    const char* m_category;
    const char* m_name;
    bool m_enabled;
};

class ScopeInt
{
  public:
    ScopeInt(const char* category, const char* name, const char* argName, int64_t value);
    ~ScopeInt();

    ScopeInt(const ScopeInt&) = delete;
    ScopeInt& operator=(const ScopeInt&) = delete;

  private:
    const char* m_category;
    const char* m_name;
    const char* m_argName;
    int64_t m_value;
    bool m_enabled;
};
}

#define YSC_TRACE_CONCAT_INNER(a, b) a##b
#define YSC_TRACE_CONCAT(a, b) YSC_TRACE_CONCAT_INNER(a, b)

#define YSC_TRACE_SCOPE(category, name) YSCTrace::Scope YSC_TRACE_CONCAT(yscTraceScope, __LINE__)(category, name)
#define YSC_TRACE_SCOPE_I(category, name, argName, value) \
    YSCTrace::ScopeInt YSC_TRACE_CONCAT(yscTraceScope, __LINE__)(category, name, argName, static_cast<int64_t>(value))

#endif

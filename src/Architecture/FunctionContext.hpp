#ifndef YSC_FUNCTION_CONTEXT_HPP
#define YSC_FUNCTION_CONTEXT_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct YSCSwitchCaseInfo
{
    uint32_t m_case = 0;
    uint64_t m_target = 0;
};

struct YSCSwitchInfo
{
    uint64_t m_address = 0;
    uint8_t m_caseCount = 0;
    uint64_t m_tableStart = 0;
    uint64_t m_tableEnd = 0;
    std::vector<YSCSwitchCaseInfo> m_cases;
};

struct YSCEnterInfo
{
    uint64_t m_address = 0;
    uint8_t m_paramCount = 0;
    uint16_t m_localCount = 0;
    uint8_t m_nameLength = 0;
    std::string m_name;
};

struct YSCFunctionContext
{
    uint64_t m_start = 0;
    std::optional<YSCEnterInfo> m_enter;
    std::optional<uint8_t> m_returnCount;
    std::map<uint64_t, YSCSwitchInfo> m_switches;
};

#endif

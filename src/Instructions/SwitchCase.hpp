#ifndef YSC_SWITCH_CASE_HPP
#define YSC_SWITCH_CASE_HPP

#include <cstdint>

#pragma pack(push, 1)
struct SwitchCase
{
    uint32_t m_case;
    int16_t m_target;
};
static_assert(sizeof(SwitchCase) == 6);
#pragma pack(pop)

#endif

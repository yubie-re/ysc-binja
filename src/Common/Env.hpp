#ifndef YSC_COMMON_ENV_HPP
#define YSC_COMMON_ENV_HPP

#include <cstddef>

bool YSCGetEnvEnabled(const char* name, bool defaultValue = false);
size_t YSCGetEnvSize(const char* name, size_t defaultValue);

#endif

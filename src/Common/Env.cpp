#include "inc.hpp"
#include "Env.hpp"
#include <cstdlib>

bool YSCGetEnvEnabled(const char* name, bool defaultValue)
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

size_t YSCGetEnvSize(const char* name, size_t defaultValue)
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

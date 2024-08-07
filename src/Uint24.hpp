#ifndef UINT24_HPP
#define UINT24_HPP

inline uint32_t Uint24(const uint8_t* arr)
{
    return (static_cast<uint32_t>(arr[2]) << 16) | (static_cast<uint32_t>(arr[1]) << 8) | static_cast<uint32_t>(arr[0]);
}

#endif
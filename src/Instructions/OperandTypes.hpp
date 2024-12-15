#ifndef OPERAND_TYPES_HPP
#define OPERAND_TYPES_HPP

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#pragma pack(1)

// Primary Template for Generic Operands
template <typename T, size_t Size>
class Operand
{
private:
    T m_Value;

public:
    Operand() : m_Value(0) {}

    explicit Operand(T value) : m_Value(value) {}

    // Conversion to the underlying type
    T ToValue() const
    {
        return m_Value;
    }

    Operand& operator=(T value)
    {
        m_Value = value;
        return *this;
    }

    // Comparison operators
    bool operator==(const Operand& other) const { return m_Value == other.m_Value; }
    bool operator!=(const Operand& other) const { return m_Value != other.m_Value; }
    bool operator<(const Operand& other) const { return m_Value < other.m_Value; }
    bool operator<=(const Operand& other) const { return m_Value <= other.m_Value; }
    bool operator>(const Operand& other) const { return m_Value > other.m_Value; }
    bool operator>=(const Operand& other) const { return m_Value >= other.m_Value; }
};

// Specialization for 24-bit Operand
template <>
class Operand<uint32_t, 3>
{
private:
    uint8_t m_Bytes[3];

public:
    Operand() : m_Bytes{0, 0, 0} {}

    explicit Operand(uint32_t value)
    {
        if (value > 0xFFFFFF)
            throw std::out_of_range("Value exceeds 24-bit range");
        m_Bytes[0] = static_cast<uint8_t>(value & 0xFF);
        m_Bytes[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        m_Bytes[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    }

    explicit Operand(const uint8_t bytes[3])
    {
        std::memcpy(m_Bytes, bytes, 3);
    }

    uint32_t ToValue() const
    {
        return (static_cast<uint32_t>(m_Bytes[2]) << 16) |
               (static_cast<uint32_t>(m_Bytes[1]) << 8) |
               static_cast<uint32_t>(m_Bytes[0]);
    }

    Operand& operator=(uint32_t value)
    {
        if (value > 0xFFFFFF)
            throw std::out_of_range("Value exceeds 24-bit range");
        m_Bytes[0] = static_cast<uint8_t>(value & 0xFF);
        m_Bytes[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        m_Bytes[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        return *this;
    }

    // Comparison operators
    bool operator==(const Operand& other) const
    {
        return std::memcmp(m_Bytes, other.m_Bytes, 3) == 0;
    }
    bool operator!=(const Operand& other) const
    {
        return !(*this == other);
    }
    bool operator<(const Operand& other) const
    {
        return ToValue() < other.ToValue();
    }
    bool operator<=(const Operand& other) const
    {
        return ToValue() <= other.ToValue();
    }
    bool operator>(const Operand& other) const
    {
        return ToValue() > other.ToValue();
    }
    bool operator>=(const Operand& other) const
    {
        return ToValue() >= other.ToValue();
    }

    const uint8_t* GetBytes() const
    {
        return m_Bytes;
    }

    uint8_t operator[](size_t index) const
    {
        if (index >= 3)
            throw std::out_of_range("Index out of range");
        return m_Bytes[index];
    }
};

#pragma pack()

// Aliases for common operand sizes
using OpU8 = Operand<uint8_t, 1>;
using OpU16 = Operand<uint16_t, 2>;
using OpU24 = Operand<uint32_t, 3>;
using OpU32 = Operand<uint32_t, 4>;
using OpS16 = Operand<int16_t, 2>;
using OpF = Operand<float, 4>;


static_assert(sizeof(OpU8) == 1);
static_assert(sizeof(OpU16) == 2);
static_assert(sizeof(OpS16) == 2);
static_assert(sizeof(OpU24) == 3);
static_assert(sizeof(OpU32) == 4);
static_assert(sizeof(OpF) == 4);

#endif

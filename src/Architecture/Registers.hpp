#ifndef YSC_REGISTERS_HPP
#define YSC_REGISTERS_HPP

#include <array>
#include <string_view>

enum Registers
{
    Reg_SP,
    Reg_FP,
    Reg_SWITCH,
    Reg_VX1,
    Reg_VY1,
    Reg_VZ1,
    Reg_VX2,
    Reg_VY2,
    Reg_VZ2,
    Reg_R1,
    Reg_R2,
    Reg_R3,
    Reg_R4,
    Reg_ARG0,
    Reg_ARG1,
    Reg_ARG2,
    Reg_ARG3,
    Reg_ARG4,
    Reg_ARG5,
    Reg_ARG6,
    Reg_ARG7,
    Reg_ARG8,
    Reg_ARG9,
    Reg_ARG10,
    Reg_ARG11,
    Reg_ARG12,
    Reg_ARG13,
    Reg_ARG14,
    Reg_ARG15,
    Reg_MAX
};

const std::array<std::string_view, Reg_MAX> g_RegNames = {
    "SP", "FP", "SWITCH", "VX1", "VY1", "VZ1", "VX2", "VY2", "VZ2", "R1", "R2", "R3", "R4",
    "ARG0", "ARG1", "ARG2", "ARG3", "ARG4", "ARG5", "ARG6", "ARG7",
    "ARG8", "ARG9", "ARG10", "ARG11", "ARG12", "ARG13", "ARG14", "ARG15",
};

#endif

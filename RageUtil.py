from io import BytesIO
import struct

# Opcodes
OP_NOP = 0
OP_IADD = 1
OP_ISUB = 2
OP_IMUL = 3
OP_IDIV = 4
OP_IMOD = 5
OP_INOT = 6
OP_INEG = 7
OP_IEQ = 8
OP_INE = 9
OP_IGT = 10
OP_IGE = 11
OP_ILT = 12
OP_ILE = 13
OP_FADD = 14
OP_FSUB = 15
OP_FMUL = 16
OP_FDIV = 17
OP_FMOD = 18
OP_FNEG = 19
OP_FEQ = 20
OP_FNE = 21
OP_FGT = 22
OP_FGE = 23
OP_FLT = 24
OP_FLE = 25
OP_VADD = 26
OP_VSUB = 27
OP_VMUL = 28
OP_VDIV = 29
OP_VNEG = 30
OP_IAND = 31
OP_IOR = 32
OP_IXOR = 33
OP_I2F = 34
OP_F2I = 35
OP_F2V = 36
OP_PUSH_CONST_U8 = 37
OP_PUSH_CONST_U8_U8 = 38
OP_PUSH_CONST_U8_U8_U8 = 39
OP_PUSH_CONST_U32 = 40
OP_PUSH_CONST_F = 41
OP_DUP = 42
OP_DROP = 43
OP_NATIVE = 44
OP_ENTER = 45
OP_LEAVE = 46
OP_LOAD = 47
OP_STORE = 48
OP_STORE_REV = 49
OP_LOAD_N = 50
OP_STORE_N = 51
OP_ARRAY_U8 = 52
OP_ARRAY_U8_LOAD = 53
OP_ARRAY_U8_STORE = 54
OP_LOCAL_U8 = 55
OP_LOCAL_U8_LOAD = 56
OP_LOCAL_U8_STORE = 57
OP_STATIC_U8 = 58
OP_STATIC_U8_LOAD = 59
OP_STATIC_U8_STORE = 60
OP_IADD_U8 = 61
OP_IMUL_U8 = 62
OP_IOFFSET = 63
OP_IOFFSET_U8 = 64
OP_IOFFSET_U8_LOAD = 65
OP_IOFFSET_U8_STORE = 66
OP_PUSH_CONST_S16 = 67
OP_IADD_S16 = 68
OP_IMUL_S16 = 69
OP_IOFFSET_S16 = 70
OP_IOFFSET_S16_LOAD = 71
OP_IOFFSET_S16_STORE = 72
OP_ARRAY_U16 = 73
OP_ARRAY_U16_LOAD = 74
OP_ARRAY_U16_STORE = 75
OP_LOCAL_U16 = 76
OP_LOCAL_U16_LOAD = 77
OP_LOCAL_U16_STORE = 78
OP_STATIC_U16 = 79
OP_STATIC_U16_LOAD = 80
OP_STATIC_U16_STORE = 81
OP_GLOBAL_U16 = 82
OP_GLOBAL_U16_LOAD = 83
OP_GLOBAL_U16_STORE = 84
OP_J = 85
OP_JZ = 86
OP_IEQ_JZ = 87
OP_INE_JZ = 88
OP_IGT_JZ = 89
OP_IGE_JZ = 90
OP_ILT_JZ = 91
OP_ILE_JZ = 92
OP_CALL = 93
OP_STATIC_U24 = 94
OP_STATIC_U24_LOAD = 95
OP_STATIC_U24_STORE = 96
OP_GLOBAL_U24 = 97
OP_GLOBAL_U24_LOAD = 98
OP_GLOBAL_U24_STORE = 99
OP_PUSH_CONST_U24 = 100
OP_SWITCH = 101
OP_STRING = 102
OP_STRINGHASH = 103
OP_TEXT_LABEL_ASSIGN_STRING = 104
OP_TEXT_LABEL_ASSIGN_INT = 105
OP_TEXT_LABEL_APPEND_STRING = 106
OP_TEXT_LABEL_APPEND_INT = 107
OP_TEXT_LABEL_COPY = 108
OP_CATCH = 109
OP_THROW = 110
OP_CALLINDIRECT = 111
OP_PUSH_CONST_M1 = 112
OP_PUSH_CONST_0 = 113
OP_PUSH_CONST_1 = 114
OP_PUSH_CONST_2 = 115
OP_PUSH_CONST_3 = 116
OP_PUSH_CONST_4 = 117
OP_PUSH_CONST_5 = 118
OP_PUSH_CONST_6 = 119
OP_PUSH_CONST_7 = 120
OP_PUSH_CONST_FM1 = 121
OP_PUSH_CONST_F0 = 122
OP_PUSH_CONST_F1 = 123
OP_PUSH_CONST_F2 = 124
OP_PUSH_CONST_F3 = 125
OP_PUSH_CONST_F4 = 126
OP_PUSH_CONST_F5 = 127
OP_PUSH_CONST_F6 = 128
OP_PUSH_CONST_F7 = 129
OP_IS_BIT_SET = 130


def get_next_byte(stream: BytesIO):
    return int.from_bytes(stream.read(1), 'little')


def get_next_dword(stream):
    return int.from_bytes(stream.read(4), 'little')


def get_next_word(stream):
    return int.from_bytes(stream.read(2), 'little', signed=True)

def get_next_uword(stream):
    return int.from_bytes(stream.read(2), 'little')


def get_next_u24(stream):
    return int.from_bytes(stream.read(3), 'little')


def get_next_float(stream):
    return struct.unpack('<f', stream.read(4))[0]


def get_next_byte_str(stream: BytesIO):
    return str(get_next_byte(stream))


def get_next_dword_str(stream):
    return str(get_next_dword(stream))


def get_next_word_str(stream):
    return str(get_next_word(stream))


def get_next_u24_str(stream):
    return str(get_next_u24(stream))


def get_next_float_str(stream):
    return str(get_next_float(stream))


def format_address(addr: int):
    return f"0x{addr:X}"

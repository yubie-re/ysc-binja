from binaryninja import Architecture, LowLevelILFunction, LowLevelILLabel, Variable, VariableSourceType
from io import BytesIO
from RageUtil import *

STACK_VADDR = 0x50000000
GLOBAL_VADDR = 0x40000000

CONST_INT_TRANSLATIONS = {
    OP_PUSH_CONST_M1: -1,
    OP_PUSH_CONST_0: 0,
    OP_PUSH_CONST_1: 1,
    OP_PUSH_CONST_2: 2,
    OP_PUSH_CONST_3: 3,
    OP_PUSH_CONST_4: 4,
    OP_PUSH_CONST_5: 5,
    OP_PUSH_CONST_6: 6,
    OP_PUSH_CONST_7: 7,
}

CONST_FLOAT_TRANSLATIONS = {
    OP_PUSH_CONST_FM1: -1.0,
    OP_PUSH_CONST_F0: 0.0,
    OP_PUSH_CONST_F1: 1.0,
    OP_PUSH_CONST_F2: 2.0,
    OP_PUSH_CONST_F3: 3.0,
    OP_PUSH_CONST_F4: 4.0,
    OP_PUSH_CONST_F5: 5.0,
    OP_PUSH_CONST_F6: 6.0,
    OP_PUSH_CONST_F7: 7.0,
}


def get_low_level_rage_il(arch: Architecture, data: bytes, address: int, il: LowLevelILFunction):
    stream = BytesIO(data)
    insn_type = get_next_byte(stream)
    size = 0
    if insn_type == OP_NOP:
        size = 1
        il.append(il.nop())
    elif insn_type == OP_ENTER:
        
        param_count = get_next_byte(stream)
        local_count = get_next_word(stream)
        name_count = get_next_byte(stream)
        size = 5
        il.append(il.push(4, il.reg(4, 'FP')))
        # fp = sp - paramCount - 1; clean OG SP
        il.append(il.set_reg(4, 'FP', il.sub(4, il.reg(4, 'SP'), il.const(4, (param_count + 1) * 4))))

        for _ in range(0, local_count):
            il.append(il.push(4, il.const(4, 0)))
        for _ in range(0, param_count):
            il.append(il.pop(4))
    elif insn_type == OP_LEAVE:
        size = 3
        param_count = get_next_byte(stream)
        return_size = get_next_byte(stream)
        """il.append(il.set_reg(4, 'TMP', il.sub(4, il.reg(4, 'SP'), il.const(4, return_size * 4))))
        il.append(il.set_reg(4, 'SP', il.add(4, il.reg(4, 'FP'), il.const(4, (param_count - 1) * 4))))
        il.append(il.set_reg(4, 'FP', il.load(4, il.add(4, il.reg(4, 'SP'), il.const(4, 2 * 4)))))
        il.append(il.set_reg(4, 'RETADDR', il.load(4, il.add(4, il.reg(4, 'SP'), il.const(4, 1 * 4)))))
        il.append(il.set_reg(4, 'SP', il.sub(
            4, il.reg(4, 'SP'), il.const(4, (param_count) * 4))))
        for i in range(0, param_count):
            tmp_at_i = il.load(4, il.add(4, il.reg(4, 'TMP'), il.const(4,i * 4)))
            il.append(il.push(4, tmp_at_i))
        if il.view:
            for func in il.view.get_functions_containing(address):
                if func.lowest_address == 0:
                    il.append(il.no_ret())
                    return
        il.append(il.ret(il.reg(4, 'RETADDR')))"""

        il.append(il.set_reg(4, 'TMP', il.sub(4, il.reg(4, 'SP'), il.const(4, return_size * 4))))
        il.append(il.set_reg(4, 'SP', il.add(4, il.reg(4, 'FP'), il.const(4, (param_count  + 1) * 4))))
        
        # sp = fp + paramCount + 1 = old fp
        # il.pop(4) = sp = fp + paramCount = newPc (ret addr)
        # il.pop(4) = sp = fp + paramCount - 1 = sp restore
        il.append(il.set_reg(4, 'FP', il.pop(4)))
        il.append(il.set_reg(4, 'RETADDR', il.pop(4)))

        for i in range(0, param_count):
            il.append(il.pop(4))

        for i in range(0, return_size):
            il.append(il.push(4, il.load(4, il.sub(4, il.reg(4, 'TMP'), il.const(4, 4 * i)))))
        
        if il.view:
            for func in il.view.get_functions_containing(address):
                if func.lowest_address == 0:
                    il.append(il.no_ret())
                    return
        il.append(il.ret(il.reg(4, 'RETADDR')))


    elif insn_type == OP_NATIVE:
        size = 4
        ret_size = get_next_byte(stream)
        param_count = (ret_size >> 2) & 63
        native_offset = ((get_next_byte(stream) << 8)
                         | get_next_byte(stream)) * 8
        il.append(il.system_call())
        il.append(il.set_reg(4, 'SP', il.sub(4, il.reg(4, 'SP'),
                  il.const(4, (ret_size + param_count) * 4))))
    elif insn_type in CONST_INT_TRANSLATIONS:
        size = 1
        il.append(il.push(4, il.const(4, CONST_INT_TRANSLATIONS[insn_type])))
    elif insn_type in CONST_FLOAT_TRANSLATIONS:
        size = 1
        il.append(il.push(4, il.float_const_single(
            CONST_FLOAT_TRANSLATIONS[insn_type])))
    elif insn_type == OP_STATIC_U8_STORE:
        stack_needle = get_next_byte(stream) * 8
        size = 2
        il.append(il.store(4, il.add(4, il.const(4, stack_needle), il.const(4, il.const(4, STACK_VADDR))), il.pop(4)))
    elif insn_type == OP_STATIC_U8_LOAD:
        stack_needle = get_next_byte(stream) * 8
        size = 2
        il.append(il.push(4, il.load(4, il.add(4, il.const(4, stack_needle), il.const(4, il.const(4, STACK_VADDR))))))
    elif insn_type == OP_STATIC_U8:
        stack_needle = get_next_byte(stream) * 8
        size = 2
        il.append(il.push(4, il.const_pointer(4, stack_needle + STACK_VADDR)))
    elif insn_type == OP_STATIC_U16_STORE:
        stack_needle = get_next_word(stream) * 8
        size = 3
        il.append(il.store(4, il.add(4, il.const(4, stack_needle), il.const(4, il.const(4, STACK_VADDR))), il.pop(4)))
    elif insn_type == OP_STATIC_U16_LOAD:
        stack_needle = get_next_word(stream) * 8
        size = 3
        il.append(il.push(4, il.load(4, il.add(4, il.const(4, stack_needle), il.const(4, il.const(4, STACK_VADDR))))))
    elif insn_type == OP_STATIC_U16:
        stack_needle = get_next_word(stream) * 8
        size = 3
        il.append(il.push(4, il.const_pointer(4, stack_needle + STACK_VADDR)))
    elif insn_type == OP_STATIC_U24_STORE:
        stack_needle = get_next_u24(stream) * 8
        size = 4
        il.append(il.store(4, il.add(4, il.const(4, stack_needle), il.const(4, il.const(4, STACK_VADDR))), il.pop(4)))
    elif insn_type == OP_STATIC_U24_LOAD:
        stack_needle = get_next_u24(stream) * 8
        size = 4
        il.append(il.push(4, il.load(4, il.add(4, il.const(4, stack_needle), il.const(4, il.const(4, STACK_VADDR))))))
    elif insn_type == OP_STATIC_U24:
        stack_needle = get_next_u24(stream) * 8
        size = 4
        il.append(il.push(4, il.const_pointer(4, stack_needle + STACK_VADDR)))
    elif insn_type == OP_LOCAL_U8_STORE:
        needle = get_next_byte(stream) * 4
        size = 2
        il.append(il.store(4, il.add(4, il.const(4,needle), il.reg(4, 'FP')), il.pop(4)))
    elif insn_type == OP_LOCAL_U8_LOAD:
        needle = get_next_byte(stream) * 4
        size = 2
        il.append(il.push(4, il.load(4, il.add(4, il.const(4,needle), il.reg(4, 'FP')))))
    elif insn_type == OP_LOCAL_U8:
        needle = get_next_byte(stream) * 4
        size = 2
        il.append(il.push(4, il.const_pointer(4, il.add(4, il.const(4,needle), il.reg(4, 'FP')))))
    elif insn_type == OP_LOCAL_U16_STORE:
        needle = get_next_word(stream) * 4
        size = 3
        il.append(il.store(4, il.add(4, il.const(4,needle), il.reg(4, 'FP')), il.pop(4)))
    elif insn_type == OP_LOCAL_U16_LOAD:
        needle = get_next_word(stream) * 4
        size = 3
        il.append(il.push(4, il.load(4, il.add(4, il.const(4,needle), il.reg(4, 'FP')))))
    elif insn_type == OP_LOCAL_U16:
        needle = get_next_word(stream) * 4
        size = 3
        il.append(il.push(4, il.const_pointer(4, il.add(4, il.const(4,needle), il.reg(4, 'FP')))))
    elif insn_type == OP_GLOBAL_U24_STORE:
        stack_needle = get_next_u24(stream)
        size = 4
        il.append(il.store(4, il.const(4, stack_needle + GLOBAL_VADDR), il.pop(4)))
    elif insn_type == OP_GLOBAL_U24_LOAD:
        stack_needle = get_next_u24(stream)
        size = 4
        il.append(il.push(4, il.load(4, il.const(4, stack_needle + GLOBAL_VADDR))))
    elif insn_type == OP_GLOBAL_U24:
        stack_needle = get_next_u24(stream)
        size = 4
        il.append(il.push(4, il.const_pointer(4, stack_needle + GLOBAL_VADDR)))
    elif insn_type == OP_GLOBAL_U16_STORE:
        stack_needle = get_next_uword(stream)
        size = 3
        il.append(il.store(4, il.const(4, stack_needle + GLOBAL_VADDR), il.pop(4)))
    elif insn_type == OP_GLOBAL_U16_LOAD:
        stack_needle = get_next_uword(stream)
        size = 3
        il.append(il.push(4, il.load(4, il.const(4, stack_needle + GLOBAL_VADDR))))
    elif insn_type == OP_GLOBAL_U16:
        stack_needle = get_next_uword(stream)
        size = 3
        il.append(il.push(4, il.const_pointer(4, stack_needle + GLOBAL_VADDR)))
    elif insn_type == OP_IOFFSET_S16:
        needle = get_next_word(stream) * 8
        size = 3
        il.append(il.push(4, il.add(4, il.pop(4), il.const(4, needle))))
    elif insn_type == OP_IOFFSET_S16_LOAD:
        needle = get_next_word(stream) * 8
        size = 3
        il.append(il.push(4, il.load(4, il.add(4, il.pop(4), il.const(4, needle)))))
    elif insn_type == OP_IOFFSET_S16_STORE:
        needle = get_next_word(stream) * 8
        size = 3
        il.append(il.store(4, il.add(4, il.pop(4), il.const(4, needle)), il.pop(4)))
    elif insn_type == OP_IOFFSET:
        size = 1
        il.append(il.push(4, il.const_pointer(4, il.add(4, il.pop(4), il.mult(4, il.pop(4), il.const(4, 8))))))
    elif insn_type == OP_IOFFSET_U8:
        needle = get_next_byte(stream) * 8
        size = 2
        il.append(il.push(4, il.const_pointer(4, il.add(4, il.pop(4), il.const(4, needle)))))
    elif insn_type == OP_IOFFSET_U8_LOAD:
        needle = get_next_byte(stream) * 8
        size = 2
        il.append(il.push(4, il.load(4, il.add(4, il.pop(4), il.const(4, needle)))))
    elif insn_type == OP_IOFFSET_U8_STORE:
        needle = get_next_byte(stream) * 8
        size = 2
        il.append(il.store(4, il.add(4, il.pop(4), il.const(4, needle)), il.pop(4)))
    elif insn_type == OP_PUSH_CONST_U8:
        size = 2
        const = get_next_byte(stream)
        il.append(il.push(4, il.const(4, const)))
    elif insn_type == OP_PUSH_CONST_U8_U8:
        size = 3
        il.append(il.push(4, il.const(4, get_next_byte(stream))))
        il.append(il.push(4, il.const(4, get_next_byte(stream))))
    elif insn_type == OP_PUSH_CONST_U8_U8_U8:
        size = 4
        il.append(il.push(4, il.const(4, get_next_byte(stream))))
        il.append(il.push(4, il.const(4, get_next_byte(stream))))
        il.append(il.push(4, il.const(4, get_next_byte(stream))))
    elif insn_type == OP_PUSH_CONST_F:
        size = 5
        if(len(data) < 5):
            size = None
            return
        const = get_next_float(stream)
        il.append(il.push(4, il.float_const_single(const)))
    elif insn_type == OP_PUSH_CONST_U24:
        size = 4
        const = get_next_u24(stream)
        il.append(il.push(4, il.const(4, const)))
    elif insn_type == OP_PUSH_CONST_S16:
        size = 3
        const = get_next_word(stream)
        il.append(il.push(4, il.const(4, const)))
    elif insn_type == OP_PUSH_CONST_U32:
        size = 5
        const = get_next_dword(stream)
        il.append(il.push(4, il.const(4, const)))
    elif insn_type == OP_STRING:
        size = 1
        str_seg_offset = 0
        if il.view:
            for section_name in il.view.sections:
                if section_name == 'STRINGS':
                    str_seg_offset = il.view.sections[section_name].start 
                    il.append(il.push(4, il.add(4, il.const(4, str_seg_offset), il.pop(4))))
                    break
    elif insn_type == OP_CALL:
        size = 4
        address = get_next_u24(stream)
        il.append(il.call(il.const(4, address)))
    elif insn_type == OP_STORE_N:
        size = 1
        il.append(il.system_call())
    elif insn_type == OP_LOAD_N:
        size = 1
        il.append(il.system_call())
    elif insn_type == OP_J:
        size = 3
        operand_one = address + get_next_word(stream) + 3
        il.append(il.jump(il.const(4, operand_one)))
    elif insn_type == OP_JZ:
        size = 3
        operand_one = address + get_next_word(stream) + 3
        b1 = LowLevelILLabel()
        b2 = LowLevelILLabel()
        il.append(il.if_expr(il.pop(4), b1, b2))
        il.mark_label(b2)
        il.append(il.jump(il.const(4, operand_one)))
        il.mark_label(b1)
    elif insn_type == OP_IEQ_JZ:
        size = 3
        operand_one = address + get_next_word(stream) + 3
        t = LowLevelILLabel()
        f = LowLevelILLabel()
        il.append(il.if_expr(il.compare_equal(4, il.pop(4), il.pop(4)), t, f))
        il.mark_label(f)
        il.append(il.jump(il.const(4, operand_one)))
        il.mark_label(t)
    elif insn_type == OP_INE_JZ:
        size = 3
        operand_one = address + get_next_word(stream) + 3
        t = LowLevelILLabel()
        f = LowLevelILLabel()
        il.append(il.if_expr(il.compare_not_equal(
            4, il.pop(4), il.pop(4)), t, f))
        il.mark_label(f)
        il.append(il.jump(il.const(4, operand_one)))
        il.mark_label(t)
    elif insn_type == OP_IGE_JZ:
        size = 3
        operand_one = address + get_next_word(stream) + 3
        t = LowLevelILLabel()
        f = LowLevelILLabel()
        il.append(il.if_expr(il.compare_signed_greater_equal(
            4, il.pop(4), il.pop(4)), t, f))
        il.mark_label(f)
        il.append(il.jump(il.const(4, operand_one)))
        il.mark_label(t)
    elif insn_type == OP_IGT_JZ:
        size = 3
        operand_one = address + get_next_word(stream) + 3
        t = LowLevelILLabel()
        f = LowLevelILLabel()
        il.append(il.if_expr(il.compare_signed_greater_than(
            4, il.pop(4), il.pop(4)), t, f))
        il.mark_label(f)
        il.append(il.jump(il.const(4, operand_one)))
        il.mark_label(t)
    elif insn_type == OP_ILE_JZ:
        size = 3
        operand_one = address + get_next_word(stream) + 3
        t = LowLevelILLabel()
        f = LowLevelILLabel()
        il.append(il.if_expr(il.compare_signed_less_equal(
            4, il.pop(4), il.pop(4)), t, f))
        il.mark_label(f)
        il.append(il.jump(il.const(4, operand_one)))
        il.mark_label(t)
    elif insn_type == OP_ILT_JZ:
        size = 3
        operand_one = address + get_next_word(stream) + 3
        t = LowLevelILLabel()
        f = LowLevelILLabel()
        il.append(il.if_expr(il.compare_signed_less_than(
            4, il.pop(4), il.pop(4)), t, f))
        il.mark_label(f)
        il.append(il.jump(il.const(4, operand_one)))
        il.mark_label(t)
    elif insn_type == OP_LOAD:
        size = 1
        il.append(il.push(4, il.load(4, il.pop(4))))
    elif insn_type == OP_STORE:
        size = 1
        il.append(il.store(4, il.pop(4), il.pop(4)))
    elif insn_type == OP_STORE_REV: # TODO: FIGURE THIS OUT
        size = 1
        il.append(il.store(4, il.pop(4), il.pop(4)))
    elif insn_type == OP_SWITCH:
        size = 1
        switch_count = get_next_byte(stream)
        if il.view:
            switch_table = il.view.read(address + 2, 6 * switch_count)
            il.append(il.set_reg(4, 'SWITCH', il.pop(4)))
            recursive_switch_il(il, switch_table, address, 0, LowLevelILLabel())
    elif insn_type == OP_IS_BIT_SET:
        size = 1
        il.append(il.push(4, il.compare_not_equal(4, il.const(4, 0), il.and_expr(
            4, il.pop(4), il.shift_left(4, il.const(4, 1), il.pop(4))))))
    elif insn_type == OP_ARRAY_U16:
        size = 3
        num = get_next_word(stream)
        r = il.pop(4)
        idx = il.pop(4)
        one_plus_idx_times_imm = il.add(4, il.const(
            4, 1), il.mult(4, il.const(4, num), idx))
        expr = il.push(4, il.add(4, r, il.mult(
            4, il.const(4, 8), one_plus_idx_times_imm)))
        il.append(expr)
    elif insn_type == OP_ARRAY_U16_LOAD:
        size = 3
        num = get_next_word(stream)
        r = il.pop(4)
        idx = il.pop(4)
        one_plus_idx_times_imm = il.add(4, il.const(
            4, 1), il.mult(4, il.const(4, num), idx))
        expr = il.push(4, il.load(4, il.add(4, r, il.mult(
            4, il.const(4, 8), one_plus_idx_times_imm))))
        il.append(expr)
    elif insn_type == OP_ARRAY_U16_STORE:
        size = 3
        num = get_next_word(stream)
        r = il.pop(4)
        idx = il.pop(4)
        one_plus_idx_times_imm = il.add(4, il.const(
            4, 1), il.mult(4, il.const(4, num), idx))
        new_r = il.add(4, r, il.mult(
            4, il.const(4, 8), one_plus_idx_times_imm))
        expr = il.store(4, new_r, il.pop(4))
        il.append(expr)

    elif insn_type == OP_ARRAY_U8:
        size = 2
        num = get_next_byte(stream)
        r = il.pop(4)
        idx = il.pop(4)
        one_plus_idx_times_imm = il.add(4, il.const(
            4, 1), il.mult(4, il.const(4, num), idx))
        expr = il.push(4, il.const_pointer(4, il.add(4, r, il.mult(
            4, il.const(4, 8), one_plus_idx_times_imm))))
        il.append(expr)
    elif insn_type == OP_ARRAY_U8_LOAD:
        size = 2
        num = get_next_byte(stream)
        r = il.pop(4)
        idx = il.pop(4)
        one_plus_idx_times_imm = il.add(4, il.const(
            4, 1), il.mult(4, il.const(4, num), idx))
        expr = il.push(4, il.load(4, il.add(4, r, il.mult(
            4, il.const(4, 8), one_plus_idx_times_imm))))
        il.append(expr)
    elif insn_type == OP_ARRAY_U8_STORE:
        size = 2
        num = get_next_byte(stream)
        r = il.pop(4)
        idx = il.pop(4)
        one_plus_idx_times_imm = il.add(4, il.const(
            4, 1), il.mult(4, il.const(4, num), idx))
        new_r = il.add(4, r, il.mult(
            4, il.const(4, 8), one_plus_idx_times_imm))
        expr = il.store(4, new_r, il.pop(4))
        il.append(expr)

    elif insn_type == OP_IADD:
        size = 1
        il.append(il.push(4, il.add(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_ISUB:
        size = 1
        il.append(il.push(4, il.sub(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_IMUL:
        size = 1
        il.append(il.push(4, il.mult(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_IDIV:
        size = 1
        il.append(il.push(4, il.div_signed(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_IMOD:
        size = 1
        il.append(il.push(4, il.mod_signed(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_INOT:
        size = 1
        il.append(il.push(4, il.compare_not_equal(
            4, il.const(4, 0), il.pop(4))))
    elif insn_type == OP_IAND:
        size = 1
        il.append(il.push(4, il.and_expr(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_IOR:
        size = 1
        il.append(il.push(4, il.or_expr(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_IXOR:
        size = 1
        il.append(il.push(4, il.xor_expr(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_INEG:
        size = 1
        il.append(il.push(4, il.neg_expr(4, il.pop(4))))

    elif insn_type == OP_IEQ:
        size = 1
        il.append(il.push(4, il.compare_equal(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_INE:
        size = 1
        il.append(il.push(4, il.compare_not_equal(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_IGE:
        size = 1
        il.append(
            il.push(4, il.compare_signed_greater_equal(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_IGT:
        size = 1
        il.append(
            il.push(4, il.compare_signed_greater_than(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_ILE:
        size = 1
        il.append(il.push(4, il.compare_signed_less_equal(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_ILT:
        size = 1
        il.append(il.push(4, il.compare_signed_less_than(4, il.pop(4), il.pop(4))))

    elif insn_type == OP_FADD:
        size = 1
        il.append(il.push(4, il.float_add(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_FSUB:
        size = 1
        il.append(il.push(4, il.float_sub(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_FMUL:
        size = 1
        il.append(il.push(4, il.float_mult(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_FDIV:
        size = 1
        il.append(il.push(4, il.float_div(4, il.pop(4), il.pop(4))))

    elif insn_type == OP_FMOD:
        size = 1
        divisor = il.pop(4)
        dividend = il.pop(4)
        quotient = il.float_div(4, dividend, divisor)
        truncated_quotient = il.float_to_int(4, quotient)
        float_truncated_quotient = il.int_to_float(4, truncated_quotient)
        product = il.float_mult(4, float_truncated_quotient, divisor)
        remainder = il.float_sub(4, dividend, product)
        il.append(il.push(4, remainder))
    elif insn_type == OP_FNEG:
        size = 1
        il.append(il.push(4, il.float_neg(4, il.pop(4))))
    elif insn_type == OP_VADD:
        size = 1
        # stack
        # -1 = v1[z]
        # -2 = v1[y]
        # -3 = v1[x]
        # -4 = v1[z]
        # -5 = v1[y]
        # -6 = v1[x]

        # pop(3)

        il.append(il.set_reg(4, 'VZ1', il.pop(4)))
        il.append(il.set_reg(4, 'VY1', il.pop(4)))
        il.append(il.set_reg(4, 'VX1', il.pop(4)))

        il.append(il.set_reg(4, 'VZ2', il.pop(4)))
        il.append(il.set_reg(4, 'VY2', il.pop(4)))
        il.append(il.set_reg(4, 'VX2', il.pop(4)))

        il.append(il.push(4, il.float_add(4, il.reg(4, 'VX2'), il.reg(4, 'VX1'))))
        il.append(il.push(4, il.float_add(4, il.reg(4, 'VY2'), il.reg(4, 'VY1'))))
        il.append(il.push(4, il.float_add(4, il.reg(4, 'VZ2'), il.reg(4, 'VZ1'))))

    elif insn_type == OP_VSUB:
        size = 1
        # Pop the top three vectors from the stack
        il.append(il.set_reg(4, 'VZ1', il.pop(4)))
        il.append(il.set_reg(4, 'VY1', il.pop(4)))
        il.append(il.set_reg(4, 'VX1', il.pop(4)))

        il.append(il.set_reg(4, 'VZ2', il.pop(4)))
        il.append(il.set_reg(4, 'VY2', il.pop(4)))
        il.append(il.set_reg(4, 'VX2', il.pop(4)))

        il.append(il.push(4, il.float_sub(4, il.reg(4, 'VX2'), il.reg(4, 'VX1'))))
        il.append(il.push(4, il.float_sub(4, il.reg(4, 'VY2'), il.reg(4, 'VY1'))))
        il.append(il.push(4, il.float_sub(4, il.reg(4, 'VZ2'), il.reg(4, 'VZ1'))))

    elif insn_type == OP_VMUL:
        size = 1
        # Pop the top three vectors from the stack
        il.append(il.set_reg(4, 'VZ1', il.pop(4)))
        il.append(il.set_reg(4, 'VY1', il.pop(4)))
        il.append(il.set_reg(4, 'VX1', il.pop(4)))

        il.append(il.set_reg(4, 'VZ2', il.pop(4)))
        il.append(il.set_reg(4, 'VY2', il.pop(4)))
        il.append(il.set_reg(4, 'VX2', il.pop(4)))

        il.append(il.push(4, il.float_mult(4, il.reg(4, 'VX2'), il.reg(4, 'VX1'))))
        il.append(il.push(4, il.float_mult(4, il.reg(4, 'VY2'), il.reg(4, 'VY1'))))
        il.append(il.push(4, il.float_mult(4, il.reg(4, 'VZ2'), il.reg(4, 'VZ1'))))

    elif insn_type == OP_VDIV:
        size = 1
        # Pop the top three vectors from the stack
        il.append(il.set_reg(4, 'VZ1', il.pop(4)))
        il.append(il.set_reg(4, 'VY1', il.pop(4)))
        il.append(il.set_reg(4, 'VX1', il.pop(4)))

        il.append(il.set_reg(4, 'VZ2', il.pop(4)))
        il.append(il.set_reg(4, 'VY2', il.pop(4)))
        il.append(il.set_reg(4, 'VX2', il.pop(4)))

        il.append(il.push(4, il.float_div(4, il.reg(4, 'VX2'), il.reg(4, 'VX1'))))
        il.append(il.push(4, il.float_div(4, il.reg(4, 'VY2'), il.reg(4, 'VY1'))))
        il.append(il.push(4, il.float_div(4, il.reg(4, 'VZ2'), il.reg(4, 'VZ1'))))
    elif insn_type == OP_VNEG:
        size = 1
        # Pop the top three vectors from the stack
        il.append(il.set_reg(4, 'VZ1', il.pop(4)))
        il.append(il.set_reg(4, 'VY1', il.pop(4)))
        il.append(il.set_reg(4, 'VX1', il.pop(4)))

        il.append(il.set_reg(4, 'VZ2', il.pop(4)))
        il.append(il.set_reg(4, 'VY2', il.pop(4)))
        il.append(il.set_reg(4, 'VX2', il.pop(4)))

        il.append(il.push(4, il.float_neg(4, il.reg(4, 'VX2'), il.reg(4, 'VX1'))))
        il.append(il.push(4, il.float_neg(4, il.reg(4, 'VY2'), il.reg(4, 'VY1'))))
        il.append(il.push(4, il.float_neg(4, il.reg(4, 'VZ2'), il.reg(4, 'VZ1'))))

    elif insn_type == OP_FEQ:
        size = 1
        il.append(il.push(4, il.float_compare_equal(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_FNE:
        size = 1
        il.append(il.push(4, il.float_compare_not_equal(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_FGE:
        size = 1
        il.append(
            il.push(4, il.float_compare_greater_equal(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_FGT:
        size = 1
        il.append(
            il.push(4, il.float_compare_greater_than(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_FLE:
        size = 1
        il.append(il.push(4, il.float_compare_less_equal(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_FLT:
        size = 1
        il.append(il.push(4, il.float_compare_less_than(4, il.pop(4), il.pop(4))))
    elif insn_type == OP_DUP:
        size = 1
        il.append(il.push(4, il.load(4, il.reg(4, 'SP'))))
    elif insn_type == OP_DROP:
        size = 1
        il.append(il.pop(4))
    elif insn_type == OP_I2F:
        size = 1
        il.append(il.push(4, il.int_to_float(4, il.pop(4))))
    elif insn_type == OP_F2I:
        size = 1
        il.append(il.push(4, il.float_to_int(4, il.pop(4))))
    elif insn_type == OP_F2V:
        size = 1
        il.append(il.push(4, il.load(4, il.reg(4, 'SP'))))
        il.append(il.push(4, il.load(4, il.reg(4, 'SP'))))
    elif insn_type == OP_IADD_U8:
        size = 2
        num = get_next_byte(stream)
        il.append(il.push(4, il.add(4, il.pop(4), il.const(4, num))))
    elif insn_type == OP_IMUL_U8:
        size = 2
        num = get_next_byte(stream)
        il.append(il.push(4, il.mult(4, il.pop(4), il.const(4, num))))
    elif insn_type == OP_IADD_S16:
        size = 3
        num = get_next_word(stream)
        il.append(il.push(4, il.add(4, il.pop(4), il.const(4, num))))
    elif insn_type == OP_IMUL_S16:
        size = 3
        num = get_next_word(stream)
        il.append(il.push(4, il.mult(4, il.pop(4), il.const(4, num))))
    elif insn_type == OP_IMUL_S16:
        size = 3
        num = get_next_word(stream)
        il.append(il.push(4, il.mult(4, il.pop(4), il.const(4, num))))
    elif insn_type == OP_TEXT_LABEL_ASSIGN_STRING:
        size = 2
        num = get_next_byte(stream)
        il.append(il.pop(4))
        il.append(il.pop(4))
    elif insn_type == OP_TEXT_LABEL_ASSIGN_INT:
        size = 2
        num = get_next_byte(stream)
        il.append(il.pop(4))
        il.append(il.pop(4))
    elif insn_type == OP_TEXT_LABEL_APPEND_STRING:
        size = 2
        num = get_next_byte(stream)
        il.append(il.pop(4))
        il.append(il.pop(4))
    elif insn_type == OP_TEXT_LABEL_APPEND_INT:
        size = 2
        num = get_next_byte(stream)
        il.append(il.pop(4))
        il.append(il.pop(4))
    elif insn_type == OP_TEXT_LABEL_COPY:
        size = 2
        num = get_next_byte(stream)
        il.append(il.pop(4))
        il.append(il.pop(4))
        il.append(il.pop(4))
    elif insn_type == OP_CATCH:
        size = 1
        il.append(il.push(4, il.const(4, -1)))
    elif insn_type == OP_THROW:
        size = 1
        il.append(il.nop())
    elif insn_type == OP_CALLINDIRECT:
        size = 1
        il.append(il.call(il.pop(4)))
    elif insn_type == OP_STRINGHASH:
        size = 1
        il.append(il.system_call())
    else:
        il.append(il.unimplemented())

    if size > 0:
        return size


def recursive_switch_il(il: LowLevelILFunction, data, address, i, parent_label):
    if (i >= len(data)):
        # il.append(il.jump(il.const(4, address + i + 2)))
        return
    case = int.from_bytes(data[i:i+4], 'little')
    target = int.from_bytes(data[i+4:i+6], 'little') + i + 6 + address + 2
    t = LowLevelILLabel()
    f = LowLevelILLabel()
    il.append(
        il.if_expr(
            il.compare_equal(4,
                             il.const(4, case),
                             il.reg(4, 'SWITCH')),
            t, f)
    )
    il.mark_label(t)
    il.append(
        il.jump(
            il.const(4, target)
        )
    )
    il.mark_label(f)
    recursive_switch_il(il, data, address, i + 6, parent_label)

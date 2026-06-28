#include "inc.hpp"
#include "Architecture/YSCArchitecture.hpp"
#include "Instructions/OperationEnum.hpp"
#include "lowlevelilinstruction.h"

bool EmitYSCTextLabelFallbackLLIL(uint8_t opcode, const uint8_t* data, size_t& len,
                                  BinaryNinja::LowLevelILFunction& il)
{
    auto emit = [&](uint32_t intrinsic, std::vector<BinaryNinja::ExprId> params, size_t instrLen) {
        il.AddInstruction(il.Intrinsic({}, intrinsic, params));
        len = instrLen;
        return true;
    };

    switch (opcode)
    {
    case OP_TEXT_LABEL_ASSIGN_STRING:
        if (!data || len < 2) return false;
        return emit(Intrin_TextLabelAssignString, {il.Pop(4), il.Pop(4), il.Const(4, data[0])}, 2);
    case OP_TEXT_LABEL_ASSIGN_INT:
        if (!data || len < 2) return false;
        return emit(Intrin_TextLabelAssignInt, {il.Pop(4), il.Pop(4), il.Const(4, data[0])}, 2);
    case OP_TEXT_LABEL_APPEND_STRING:
        if (!data || len < 2) return false;
        return emit(Intrin_TextLabelAppendString, {il.Pop(4), il.Pop(4), il.Const(4, data[0])}, 2);
    case OP_TEXT_LABEL_APPEND_INT:
        if (!data || len < 2) return false;
        return emit(Intrin_TextLabelAppendInt, {il.Pop(4), il.Pop(4), il.Const(4, data[0])}, 2);
    case OP_TEXT_LABEL_COPY:
        if (len < 1) return false;
        return emit(Intrin_TextLabelCopy, {il.Pop(4), il.Pop(4), il.Pop(4)}, 1);
    default:
        return false;
    }
}

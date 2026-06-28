#ifndef YSC_INTRINSICS_HPP
#define YSC_INTRINSICS_HPP

#include <array>
#include <string_view>

enum Intrin
{
    Intrin_StringHash,
    Intrin_TextLabelAssignString,
    Intrin_TextLabelAssignInt,
    Intrin_TextLabelAppendString,
    Intrin_TextLabelAppendInt,
    Intrin_TextLabelCopy,
    Intrin_MAX
};

const std::array<std::string_view, Intrin_MAX> g_intrinNames = {
    "ysc_string_hash",
    "ysc_text_label_assign_string",
    "ysc_text_label_assign_int",
    "ysc_text_label_append_string",
    "ysc_text_label_append_int",
    "ysc_text_label_copy",
};

#endif

from dataclasses import dataclass
import ctypes
import os
from io import BytesIO
from struct import unpack
from binaryninja.binaryview import BinaryView, SectionSemantics
from binaryninja import Platform, Architecture, Symbol, SymbolType, StructureBuilder, Type
from binaryninja.enums import SegmentFlag

@dataclass
class RageScriptHeader:
    table : int
    opcode_size : int
    arg_struct_size : int
    static_size : int
    global_size : int
    native_size : int
    statics : int
    scr_globals : int
    natives : int
    hash_code : int
    ref_count : int
    script_name : int
    string_heaps: int
    string_heaps_size : int


def load_script_header(view : BinaryView):
    f = BytesIO(view.read(0, 1000))
    f.seek(0)
    f.read(16)
    table = int.from_bytes(f.read(8), "little") & 0xFFFFFF
    print("{:x}".format(table))
    f.read(4)
    opcode_size = int.from_bytes(f.read(4), "little")
    print("{:x}".format(opcode_size))
    arg_struct_size = int.from_bytes(f.read(4), "little")
    static_size = int.from_bytes(f.read(4), "little")
    global_size = int.from_bytes(f.read(4), "little")
    native_size = int.from_bytes(f.read(4), "little")
    statics = int.from_bytes(f.read(8), "little") & 0xFFFFFF
    scr_globals = int.from_bytes(f.read(8), "little") & 0xFFFFFF
    natives = int.from_bytes(f.read(8), "little") & 0xFFFFFF
    print(natives)
    f.read(16)
    hash_code = int.from_bytes(f.read(4), "little")
    ref_count = int.from_bytes(f.read(4), "little")
    script_name = int.from_bytes(f.read(8), "little") & 0xFFFFFF
    string_heaps = int.from_bytes(f.read(8), "little") & 0xFFFFFF
    string_heaps_size = int.from_bytes(f.read(4), "little")
    f.read(12)
    return RageScriptHeader(table, opcode_size, arg_struct_size, static_size, global_size, native_size, statics, scr_globals, natives, hash_code, ref_count, script_name, string_heaps, string_heaps_size), f.tell()


def rotate_left(value, count):
    count &= 63
    return ctypes.c_uint64((value << count) | (value >> (64 - count))).value

class RageView(BinaryView):
    name = 'RAGEView'
    long_name = 'Grand Theft Auto V YSC RAGE Script'
    data : BinaryView = None
    PAGE_SIZE = 0x4000

    def __init__(self, data):
        BinaryView.__init__(self, parent_view = data, file_metadata = data.file)
        self.arch = Architecture['YSC']
        self.platform = Architecture['YSC'].standalone_platform
        #self.platform = Platform['']
        self.data = data
        self.data.arch = Architecture['YSC']
        self.data.platform = Architecture['YSC'].standalone_platform

    def get_page_size(self, page_index, page_count, total_size):
        if page_index < 0 or page_index >= page_count:
            return 0
        if page_index == page_count - 1:
            return total_size % self.PAGE_SIZE or self.PAGE_SIZE
        return self.PAGE_SIZE

    @classmethod
    def is_valid_for_data(self, data : BinaryView):
        header, size = load_script_header(data)
        sanity_checks = header.global_size == 0
        return sanity_checks and 'ysc' in data.file.filename

    def perform_get_address_size(self):
        return 4
    
    def get_page_table_array(self, table_offset, total_size) -> list[tuple[int, bytes]]:
        array = []
        offset = 0
        page_count = (total_size + self.PAGE_SIZE - 1) // self.PAGE_SIZE
        for i in range(page_count):
            file_page_address = int.from_bytes(self.data.read(table_offset + i * 8, 8), 'little') & 0xFFFFFF
            page_size = self.get_page_size(i, page_count, total_size)
            page_data = self.data.read(file_page_address, page_size)
            array.append((offset, page_data))
            offset += page_size
        return array
    
    def write_page_data(self, segment_start, arr):
        for page_info in arr:
            self.data.write(segment_start + page_info[0], page_info[1])

    def define_custom_header_structure(self, bv : BinaryView):
        sb = StructureBuilder.create()
        sb.packed = True
        sb.append(Type.int(8), "page_base")
        sb.append(Type.int(8), "map_ptr")
        sb.append(Type.int(8), "opcode_table")
        sb.append(Type.int(4), "globals_signature")
        sb.append(Type.int(4), "opcode_size")
        sb.append(Type.int(4), "parameter_count")
        sb.append(Type.int(4), "static_size")
        sb.append(Type.int(4), "global_size")
        sb.append(Type.int(4), "native_size")
        sb.append(Type.int(8), "statics_ptr")
        sb.append(Type.int(8), "globals_ptr")
        sb.append(Type.int(8), "natives_ptr")
        sb.append(Type.int(8), "unk_1")
        sb.append(Type.int(8), "unk_2")
        sb.append(Type.int(4), "hash_code")
        sb.append(Type.int(4), "ref_count")
        sb.append(Type.int(8), "script_name")
        sb.append(Type.int(8), "string_heaps")
        sb.append(Type.int(4), "string_heaps_size")
        sb.append(Type.int(8), "unk_3")
        sb.append(Type.int(4), "unk_4")

        bv.define_user_type("RageScriptHeader", sb)

    def init(self):
        header, size = load_script_header(self.data)
        self.define_custom_header_structure(self.data)
        
        #self.data.add_auto_segment(0, size, 0, size, SegmentFlag.SegmentReadable | SegmentFlag.SegmentContainsData)
        #self.data.add_auto_section("HDR", 0, header.opcode_size, SectionSemantics.ReadOnlyCodeSectionSemantics)

        opcode_offset = 0
        self.data.add_auto_segment(opcode_offset, header.opcode_size, opcode_offset, header.opcode_size, SegmentFlag.SegmentReadable | SegmentFlag.SegmentExecutable | SegmentFlag.SegmentContainsCode)
        self.data.add_auto_section("CODE", opcode_offset, header.opcode_size, SectionSemantics.ReadOnlyCodeSectionSemantics)

        native_segment_offset = opcode_offset + header.opcode_size
        self.data.add_auto_segment(native_segment_offset, header.native_size + header.string_heaps_size + header.static_size + header.global_size, 0, header.native_size, SegmentFlag.SegmentReadable | SegmentFlag.SegmentContainsData)
        self.data.add_auto_section("NATIVES", native_segment_offset, header.native_size, SectionSemantics.ReadOnlyDataSectionSemantics)

        string_segment_offset = native_segment_offset + header.native_size
        self.data.add_auto_section("STRINGS", string_segment_offset, header.string_heaps_size, SectionSemantics.ReadOnlyDataSectionSemantics)

        static_segment_offset = string_segment_offset + header.string_heaps_size
        self.data.add_auto_section("STATICS", static_segment_offset, header.static_size, SectionSemantics.ReadOnlyDataSectionSemantics)

        global_segment_offset = static_segment_offset + header.static_size
        if(header.scr_globals != 0 and header.global_size != 0):
            self.data.add_auto_section("GLOBALS", global_segment_offset, header.global_size, SectionSemantics.ReadOnlyDataSectionSemantics)

        self.data.add_auto_section("GLOBAL_MEMORY", 0x40000000, 0x100000, SectionSemantics.ReadOnlyDataSectionSemantics)
        

        
        print("Caching pages")
        global_pages = []
        opcode_pages = self.get_page_table_array(header.table, header.opcode_size)
        string_pages = self.get_page_table_array(header.string_heaps, header.string_heaps_size)
        static_pages = self.get_page_table_array(header.statics, header.static_size)
        if(header.scr_globals != 0 and header.global_size != 0):
            global_pages = self.get_page_table_array(header.scr_globals, header.global_size)

        natives = []
        for i in range(0, header.native_size // 8):
            natives.append(rotate_left(ctypes.c_uint64(int.from_bytes(self.data.read(header.natives + i * 8, 8), 'little')).value, i + native_segment_offset))

        self.write_page_data(opcode_offset, opcode_pages)
        self.write_page_data(string_segment_offset, string_pages)
        self.write_page_data(static_segment_offset, static_pages)
        self.write_page_data(global_segment_offset, global_pages)

        self.data.remove(global_segment_offset + header.global_size, self.data.length - global_segment_offset - header.global_size)

        #self.data.define_data_var(0, self.data.types["RageScriptHeader"])

        for i in range(0, len(natives)):
            native = natives[i]
            self.data.write(native_segment_offset + (i * 8), native.to_bytes(8, 'little'))
            self.data.define_data_var(native_segment_offset + (i * 8), "uint64_t")
            self.data.define_auto_symbol(Symbol(SymbolType.DataSymbol, native_segment_offset + (i * 8), "native_{:X}".format(native)))


        self.data.add_entry_point(opcode_offset, Architecture['YSC'].standalone_platform)
        return True
    
    def perform_is_executable(self):
        return True

    def perform_get_entry_point(self):
        return 0

RageView.register()
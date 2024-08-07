from dataclasses import dataclass
import ctypes
import os
from io import BytesIO
from struct import unpack
from binaryninja.binaryview import BinaryView, SectionSemantics
from binaryninja import Platform, Architecture, Symbol, SymbolType, StructureBuilder, Type
from binaryninja.enums import SegmentFlag

class RageScriptHeader:
    instruction_table : int
    instruction_size : int
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

    header_size : int

    def __init__(self, view : BinaryView) -> None:
        f = BytesIO(view.read(0, 1000))
        f.seek(0)
        f.read(16)
        self.instruction_table = int.from_bytes(f.read(8), "little") & 0xFFFFFF
        f.read(4)
        self.instruction_size = int.from_bytes(f.read(4), "little")
        self.arg_struct_size = int.from_bytes(f.read(4), "little")
        self.static_size = int.from_bytes(f.read(4), "little")
        self.global_size = int.from_bytes(f.read(4), "little")
        self.native_size = int.from_bytes(f.read(4), "little")
        self.statics = int.from_bytes(f.read(8), "little") & 0xFFFFFF
        self.scr_globals = int.from_bytes(f.read(8), "little") & 0xFFFFFF
        self.natives = int.from_bytes(f.read(8), "little") & 0xFFFFFF
        f.read(16)
        self.hash_code = int.from_bytes(f.read(4), "little")
        self.ref_count = int.from_bytes(f.read(4), "little")
        self.script_name = int.from_bytes(f.read(8), "little") & 0xFFFFFF
        self.string_heaps = int.from_bytes(f.read(8), "little") & 0xFFFFFF
        self.string_heaps_size = int.from_bytes(f.read(4), "little")
        f.read(12)
        self.header_size = f.tell()

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
        self.data = data

    def get_page_size(self, page_index, page_count, total_size):
        if page_index < 0 or page_index >= page_count:
            return 0
        if page_index == page_count - 1:
            return total_size % self.PAGE_SIZE or self.PAGE_SIZE
        return self.PAGE_SIZE

    """
    Checks if data is valid YSC format. Checks if first instruction is an ENTER, otherwise it's invalid.

    :return: is valid
    :rtype: bool
    """
    @classmethod
    def is_valid_for_data(self, data : BinaryView) -> bool:
        self.header = RageScriptHeader(data)
        start_insn_address = int.from_bytes(data.read(self.header.instruction_table, 8), 'little') & 0xFFFFFF
        start_insn = data.read(start_insn_address, 1)
        if start_insn != b'\x2D': # ENTER
            return False
        return True

    def perform_get_address_size(self):
        return 4

    """
    Given a table, get all the pages and associated virtual address for each
    """ 
    def get_page_table_array(self, table_offset, total_size) -> list[tuple[int, int, int]]:
        array = []
        offset = 0
        page_count = (total_size + self.PAGE_SIZE - 1) // self.PAGE_SIZE
        for i in range(page_count):
            file_page_address = int.from_bytes(self.data.read(table_offset + i * 8, 8), 'little') & 0xFFFFFF
            page_size = self.get_page_size(i, page_count, total_size)
            array.append((file_page_address, offset, page_size))
            offset += page_size
        return array
    
    #def write_pages(self, arr : list[tuple[int, int]], start_virtual_offset : int):
    #    for file_address, virtual_address, page_size in arr:
    #        print(file_address, virtual_address, page_size)
    #        print(self.write(virtual_address + start_virtual_offset, self.data.read(file_address, page_size)))
    
    def write_pages(self, arr : list[tuple[int, int]], start_virtual_offset : int, flags : SegmentFlag, name : str, semantics=SectionSemantics.DefaultSectionSemantics):
        size  = 0
        for file_address, virtual_address, page_size in arr:
            size += page_size
            self.add_auto_segment(virtual_address + start_virtual_offset, page_size, file_address, page_size, flags)
        self.add_auto_section(name, start_virtual_offset, size, semantics)

    def define_custom_header_structure(self, bv : BinaryView):
        b1 = StructureBuilder.create()
        b1.packed = True
        
        b1.append(Type.pointer_of_width(3, Type.int(8)))
        b1.append(Type.array(Type.int(1), 5), "ptr_pad")
        bv.define_user_type("RagePtr", b1)

        sb = StructureBuilder.create()
        sb.packed = True
        sb.append(Type.int(8), "page_base")
        sb.append(bv.types["RagePtr"], "map_ptr")
        sb.append(bv.types["RagePtr"], "instruction_table")
        sb.append(Type.int(4), "globals_signature")
        sb.append(Type.int(4), "instruction_size")
        sb.append(Type.int(4), "parameter_count")
        sb.append(Type.int(4), "static_size")
        sb.append(Type.int(4), "global_size")
        sb.append(Type.int(4), "native_size")
        sb.append(bv.types["RagePtr"], "statics_ptr")
        sb.append(bv.types["RagePtr"], "globals_ptr")
        sb.append(bv.types["RagePtr"], "natives_ptr")
        sb.append(Type.int(8), "unk_1")
        sb.append(Type.int(8), "unk_2")
        sb.append(Type.int(4), "hash_code")
        sb.append(Type.int(4), "ref_count")
        sb.append(bv.types["RagePtr"], "script_name")
        sb.append(bv.types["RagePtr"], "string_heaps")
        sb.append(Type.int(4), "string_heaps_size")
        sb.append(Type.int(8), "unk_3")
        sb.append(Type.int(4), "unk_4")

        bv.define_user_type("RageScriptHeader", sb)

    def init_raw(self):
        self.define_custom_header_structure(self.data)
        self.data.define_data_var(0, self.data.types["RageScriptHeader"])


    def init(self):
        self.arch = Architecture['YSC']
        self.platform = Architecture['YSC'].standalone_platform
        #header, size = load_script_header(self.data)
        self.init_raw()
        instruction_offset = 0
        string_offset = instruction_offset + self.header.instruction_size
        static_offset = string_offset + self.header.string_heaps_size
        native_offset = static_offset + self.header.static_size
        mem_end = native_offset + self.header.native_size
        instruction_pages = self.get_page_table_array(self.header.instruction_table, self.header.instruction_size)
        string_pages = self.get_page_table_array(self.header.string_heaps, self.header.string_heaps_size)
        static_pages = self.get_page_table_array(self.header.statics, self.header.static_size) 

        self.add_auto_segment(0x50000000, 0x10000000, 0, 0,  SegmentFlag.SegmentReadable | SegmentFlag.SegmentWritable | SegmentFlag.SegmentContainsData)
        self.add_auto_section('STACK_MEM', 0x50000000, 0x10000000, SectionSemantics.ReadWriteDataSectionSemantics)

        self.add_auto_segment(0x40000000, 0x10000000, 0, 0,  SegmentFlag.SegmentReadable | SegmentFlag.SegmentWritable | SegmentFlag.SegmentContainsData)
        self.add_auto_section('GLOBAL_MEM', 0x40000000, 0x10000000, SectionSemantics.ReadWriteDataSectionSemantics)

        self.write_pages(instruction_pages, instruction_offset, SegmentFlag.SegmentContainsCode | SegmentFlag.SegmentExecutable | SegmentFlag.SegmentReadable, 'CODE', SectionSemantics.ReadOnlyCodeSectionSemantics)
        self.write_pages(string_pages, string_offset, SegmentFlag.SegmentContainsData | SegmentFlag.SegmentReadable, 'STRING', SectionSemantics.ReadOnlyDataSectionSemantics)
        self.write_pages(static_pages, static_offset, SegmentFlag.SegmentContainsData | SegmentFlag.SegmentReadable, 'STATIC', SectionSemantics.ReadOnlyDataSectionSemantics)

        self.add_auto_segment(native_offset, self.header.native_size, self.header.natives, self.header.native_size, SegmentFlag.SegmentReadable | SegmentFlag.SegmentContainsData)
        self.add_auto_section('NATIVES', native_offset, self.header.native_size)
        for i in range(0, self.header.native_size // 8):
            native = rotate_left(ctypes.c_uint64(int.from_bytes(self.data.read(self.header.natives + i * 8, 8), 'little')).value, i + self.header.natives)
            self.write(native_offset + (i * 8), native.to_bytes(8, 'little'))
            self.define_data_var(native_offset + (i * 8), "uint64_t")
            self.define_auto_symbol(Symbol(SymbolType.DataSymbol, native_offset + (i * 8), "native_{:X}".format(native)))
        self.add_entry_point(0)
        return True
    
    def perform_is_executable(self):
        return True

    def perform_get_entry_point(self):
        return 0


from binaryninja.architecture import Architecture
from binaryninja.function import RegisterInfo, InstructionInfo
from io import BytesIO
from RageScriptDisasm import get_branch_info, dis_insn
from RageScriptIL import get_low_level_rage_il
from RageScriptCallingConvention import RageCallConvention
class YSC(Architecture):
	name = 'YSC'

	address_size = 4
	default_int_size = 1
	instr_alignment = 1
	max_instr_length = 256
	# register related stuff
	regs = {
		# main registers
		'SP': RegisterInfo('SP', 4),
		'FP': RegisterInfo('FP', 4),
		'TMP': RegisterInfo('TMP', 4), # temporary storage for certain things
		'RETADDR': RegisterInfo('RETADDR', 4), # temporary storage for certain things
		'SWITCH': RegisterInfo('SWITCH', 4), # Switch temp storage
		'VX1': RegisterInfo('VX1', 4), # Switch temp storage
		'VY1': RegisterInfo('VY1', 4), # Switch temp storage
		'VZ1': RegisterInfo('VZ1', 4), # Switch temp storage
		'VX2': RegisterInfo('VX2', 4), # Switch temp storage
		'VY2': RegisterInfo('VY2', 4), # Switch temp storage
		'VZ2': RegisterInfo('VZ2', 4), # Switch temp storage
		

		# program counter
		'PC': RegisterInfo('PC', 4),
	}

	stack_pointer = "SP"

	def get_instruction_info(self, data, addr):
		result = InstructionInfo()
		stream = BytesIO(data)
		stream.seek(0)
		_, size = dis_insn(stream, addr)
		#print(_, size)
		stream.seek(0)
		get_branch_info(stream, addr, result)
	
		result.length = size
		return result

	def get_instruction_text(self, data, addr):
		stream = BytesIO(data)
		stream.seek(0)
		return dis_insn(stream, addr)

	def get_instruction_low_level_il(self, data, addr, il):
		return get_low_level_rage_il(self, data, addr, il)

YSC.register()
Architecture['YSC'].register_calling_convention(RageCallConvention(Architecture['YSC'], 'ysc'))
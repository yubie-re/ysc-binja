from binaryninja import Architecture, CallingConvention
from binaryninja._binaryninjacore import max_confidence

class RageCallConvention(CallingConvention):
    name = "ysc_cc"
    stack_adjusted_on_return = True # ???
    callee_saved_regs = ['FP', 'SP']
    pass

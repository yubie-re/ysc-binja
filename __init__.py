from RageScriptArch import YSC
from binaryninja import Architecture
from RageScriptCallingConvention import RageCallConvention
from RageScriptView import RageView

YSC.register()
Architecture['YSC'].register_calling_convention(RageCallConvention(Architecture['YSC'], 'ysc'))
RageView.register()
#include "binaryninjaapi.h"
using namespace BinaryNinja;
#include "Architecture/YSCArchitecture.hpp"
#include "View/YSCView.hpp"
#include "CallingConvention/CallingConvention.hpp"


extern "C" {
	BN_DECLARE_CORE_ABI_VERSION

	BINARYNINJAPLUGIN bool CorePluginInit()
	{
		BinaryNinja::Architecture *ysc = new YSCArchitecture("YSC");
		BinaryNinja::Architecture::Register(ysc);
		BinaryNinja::BinaryViewType::Register(new YSCViewType());

		BinaryNinja::Ref<BinaryNinja::CallingConvention> conv = new YSCCallingConvention(ysc);
		ysc->RegisterCallingConvention(conv);
		ysc->SetDefaultCallingConvention(conv);
		ysc->SetCdeclCallingConvention(conv);
		ysc->SetStdcallCallingConvention(conv);
		ysc->SetFastcallCallingConvention(conv);
		return true;
	}
}
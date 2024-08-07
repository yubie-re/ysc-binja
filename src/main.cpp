#include "binaryninjaapi.h"
using namespace BinaryNinja;
#include "Architecture/YSCArchitecture.hpp"
#include "View/YSCView.hpp"


extern "C" {
	BN_DECLARE_CORE_ABI_VERSION

	BINARYNINJAPLUGIN bool CorePluginInit()
	{
		BinaryNinja::Architecture::Register(new YSCArchitecture("YSC"));
		BinaryNinja::BinaryViewType::Register(new YSCViewType());
		return true;
	}
}
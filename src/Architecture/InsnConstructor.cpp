#include "inc.hpp"
#include "YSCArchitecture.hpp"
#include "Instructions/OperationBase.hpp"
#include "Instructions/OperandTypes.hpp"
#include "Instructions/SubOperations/OpNop.hpp"
#include "Instructions/SubOperations/OpIadd.hpp"
#include "Instructions/SubOperations/OpIsub.hpp"
#include "Instructions/SubOperations/OpImul.hpp"
#include "Instructions/SubOperations/OpIdiv.hpp"
#include "Instructions/SubOperations/OpImod.hpp"
#include "Instructions/SubOperations/OpInot.hpp"
#include "Instructions/SubOperations/OpIneg.hpp"
#include "Instructions/SubOperations/OpIeq.hpp"
#include "Instructions/SubOperations/OpIne.hpp"
#include "Instructions/SubOperations/OpIgt.hpp"
#include "Instructions/SubOperations/OpIge.hpp"
#include "Instructions/SubOperations/OpIlt.hpp"
#include "Instructions/SubOperations/OpIle.hpp"
#include "Instructions/SubOperations/OpFadd.hpp"
#include "Instructions/SubOperations/OpFsub.hpp"
#include "Instructions/SubOperations/OpFmul.hpp"
#include "Instructions/SubOperations/OpFdiv.hpp"
#include "Instructions/SubOperations/OpFmod.hpp"
#include "Instructions/SubOperations/OpFneg.hpp"
#include "Instructions/SubOperations/OpFeq.hpp"
#include "Instructions/SubOperations/OpFne.hpp"
#include "Instructions/SubOperations/OpFgt.hpp"
#include "Instructions/SubOperations/OpFge.hpp"
#include "Instructions/SubOperations/OpFlt.hpp"
#include "Instructions/SubOperations/OpFle.hpp"
#include "Instructions/SubOperations/OpVadd.hpp"
#include "Instructions/SubOperations/OpVsub.hpp"
#include "Instructions/SubOperations/OpVmul.hpp"
#include "Instructions/SubOperations/OpVdiv.hpp"
#include "Instructions/SubOperations/OpVneg.hpp"
#include "Instructions/SubOperations/OpIand.hpp"
#include "Instructions/SubOperations/OpIor.hpp"
#include "Instructions/SubOperations/OpIxor.hpp"
#include "Instructions/SubOperations/OpI2F.hpp"
#include "Instructions/SubOperations/OpF2I.hpp"
#include "Instructions/SubOperations/OpF2V.hpp"
#include "Instructions/SubOperations/OpPushConstU8.hpp"
#include "Instructions/SubOperations/OpPushConstU8U8.hpp"
#include "Instructions/SubOperations/OpPushConstU8U8U8.hpp"
#include "Instructions/SubOperations/OpPushConstU32.hpp"
#include "Instructions/SubOperations/OpPushConstF.hpp"
#include "Instructions/SubOperations/OpDup.hpp"
#include "Instructions/SubOperations/OpDrop.hpp"
#include "Instructions/SubOperations/OpNative.hpp"
#include "Instructions/SubOperations/OpEnter.hpp"
#include "Instructions/SubOperations/OpLeave.hpp"
#include "Instructions/SubOperations/OpLoad.hpp"
#include "Instructions/SubOperations/OpStore.hpp"
#include "Instructions/SubOperations/OpStoreRev.hpp"
#include "Instructions/SubOperations/OpLoadN.hpp"
#include "Instructions/SubOperations/OpStoreN.hpp"
#include "Instructions/SubOperations/OpArray.hpp"
#include "Instructions/SubOperations/OpArrayLoad.hpp"
#include "Instructions/SubOperations/OpArrayStore.hpp"
#include "Instructions/SubOperations/OpLocalU8.hpp"
#include "Instructions/SubOperations/OpLocalU8Load.hpp"
#include "Instructions/SubOperations/OpLocalU8Store.hpp"
#include "Instructions/SubOperations/OpStatic.hpp"
#include "Instructions/SubOperations/OpStaticLoad.hpp"
#include "Instructions/SubOperations/OpStaticStore.hpp"
#include "Instructions/SubOperations/OpIaddU8.hpp"
#include "Instructions/SubOperations/OpImulU8.hpp"
#include "Instructions/SubOperations/OpIoffset.hpp"
#include "Instructions/SubOperations/OpIoffsetU8.hpp"
#include "Instructions/SubOperations/OpIoffsetU8Load.hpp"
#include "Instructions/SubOperations/OpIoffsetU8Store.hpp"
#include "Instructions/SubOperations/OpPushConstS16.hpp"
#include "Instructions/SubOperations/OpIaddS16.hpp"
#include "Instructions/SubOperations/OpImulS16.hpp"
#include "Instructions/SubOperations/OpIoffsetS16.hpp"
#include "Instructions/SubOperations/OpIoffsetS16Load.hpp"
#include "Instructions/SubOperations/OpIoffsetS16Store.hpp"
#include "Instructions/SubOperations/OpLocalU16.hpp"
#include "Instructions/SubOperations/OpLocalU16Load.hpp"
#include "Instructions/SubOperations/OpLocalU16Store.hpp"
#include "Instructions/SubOperations/OpGlobal.hpp"
#include "Instructions/SubOperations/OpGlobalLoad.hpp"
#include "Instructions/SubOperations/OpGlobalStore.hpp"
#include "Instructions/SubOperations/OpJ.hpp"
#include "Instructions/SubOperations/OpJz.hpp"
#include "Instructions/SubOperations/OpIeqJz.hpp"
#include "Instructions/SubOperations/OpIneJz.hpp"
#include "Instructions/SubOperations/OpIgtJz.hpp"
#include "Instructions/SubOperations/OpIgeJz.hpp"
#include "Instructions/SubOperations/OpIltJz.hpp"
#include "Instructions/SubOperations/OpIleJz.hpp"
#include "Instructions/SubOperations/OpCall.hpp"
#include "Instructions/SubOperations/OpPushConstU24.hpp"
#include "Instructions/SubOperations/OpSwitch.hpp"
#include "Instructions/SubOperations/OpString.hpp"
#include "Instructions/SubOperations/OpStringhash.hpp"
#include "Instructions/SubOperations/OpTextLabelAssignString.hpp"
#include "Instructions/SubOperations/OpTextLabelAssignInt.hpp"
#include "Instructions/SubOperations/OpTextLabelAppendString.hpp"
#include "Instructions/SubOperations/OpTextLabelAppendInt.hpp"
#include "Instructions/SubOperations/OpTextLabelCopy.hpp"
#include "Instructions/SubOperations/OpCatch.hpp"
#include "Instructions/SubOperations/OpThrow.hpp"
#include "Instructions/SubOperations/OpCallindirect.hpp"
#include "Instructions/SubOperations/OpPushConst.hpp"
#include "Instructions/SubOperations/OpPushConstFloat.hpp"
#include "Instructions/SubOperations/OpIsBitSet.hpp"
#include "Instructions/OperationEnum.hpp"
#include "CallingConvention/CallingConvention.hpp"

YSCArchitecture::YSCArchitecture(const std::string& name) : BinaryNinja::Architecture(name), m_insns({
        std::make_unique<OpNop>(),
        std::make_unique<OpIadd>(),
        std::make_unique<OpIsub>(),
        std::make_unique<OpImul>(),
        std::make_unique<OpIdiv>(),
        std::make_unique<OpImod>(),
        std::make_unique<OpInot>(),
        std::make_unique<OpIneg>(),
        std::make_unique<OpIeq>(),
        std::make_unique<OpIne>(),
        std::make_unique<OpIgt>(),
        std::make_unique<OpIge>(),
        std::make_unique<OpIlt>(),
        std::make_unique<OpIle>(),
        std::make_unique<OpFadd>(),
        std::make_unique<OpFsub>(),
        std::make_unique<OpFmul>(),
        std::make_unique<OpFdiv>(),
        std::make_unique<OpFmod>(),
        std::make_unique<OpFneg>(),
        std::make_unique<OpFeq>(),
        std::make_unique<OpFne>(),
        std::make_unique<OpFgt>(),
        std::make_unique<OpFge>(),
        std::make_unique<OpFlt>(),
        std::make_unique<OpFle>(),
        std::make_unique<OpVadd>(),
        std::make_unique<OpVsub>(),
        std::make_unique<OpVmul>(),
        std::make_unique<OpVdiv>(),
        std::make_unique<OpVneg>(),
        std::make_unique<OpIand>(),
        std::make_unique<OpIor>(),
        std::make_unique<OpIxor>(),
        std::make_unique<OpI2F>(),
        std::make_unique<OpF2I>(),
        std::make_unique<OpF2V>(),
        std::make_unique<OpPushConstU8>(),
        std::make_unique<OpPushConstU8U8>(),
        std::make_unique<OpPushConstU8U8U8>(),
        std::make_unique<OpPushConstU32>(),
        std::make_unique<OpPushConstF>(),
        std::make_unique<OpDup>(),
        std::make_unique<OpDrop>(),
        std::make_unique<OpNative>(),
        std::make_unique<OpEnter>(),
        std::make_unique<OpLeave>(),
        std::make_unique<OpLoad>(),
        std::make_unique<OpStore>(),
        std::make_unique<OpStoreRev>(),
        std::make_unique<OpLoadN>(),
        std::make_unique<OpStoreN>(),
        std::make_unique<OpArray<OpU8>>(),
        std::make_unique<OpArrayLoad<OpU8>>(),
        std::make_unique<OpArrayStore<OpU8>>(),
        std::make_unique<OpLocalU8>(),
        std::make_unique<OpLocalU8Load>(),
        std::make_unique<OpLocalU8Store>(),
        std::make_unique<OpStatic<OpU8>>(),
        std::make_unique<OpStaticLoad<OpU8>>(),
        std::make_unique<OpStaticStore<OpU8>>(),
        std::make_unique<OpIaddU8>(),
        std::make_unique<OpImulU8>(),
        std::make_unique<OpIoffset>(),
        std::make_unique<OpIoffsetU8>(),
        std::make_unique<OpIoffsetU8Load>(),
        std::make_unique<OpIoffsetU8Store>(),
        std::make_unique<OpPushConstS16>(),
        std::make_unique<OpIaddS16>(),
        std::make_unique<OpImulS16>(),
        std::make_unique<OpIoffsetS16>(),
        std::make_unique<OpIoffsetS16Load>(),
        std::make_unique<OpIoffsetS16Store>(),
        std::make_unique<OpArray<OpU16>>(),
        std::make_unique<OpArrayLoad<OpU16>>(),
        std::make_unique<OpArrayStore<OpU16>>(),
        std::make_unique<OpLocalU16>(),
        std::make_unique<OpLocalU16Load>(),
        std::make_unique<OpLocalU16Store>(),
        std::make_unique<OpStatic<OpU16>>(),
        std::make_unique<OpStaticLoad<OpU16>>(),
        std::make_unique<OpStaticStore<OpU16>>(),
        std::make_unique<OpGlobal<OpU16>>(),
        std::make_unique<OpGlobalLoad<OpU16>>(),
        std::make_unique<OpGlobalStore<OpU16>>(),
        std::make_unique<OpJ>(),
        std::make_unique<OpJz>(),
        std::make_unique<OpIeqJz>(),
        std::make_unique<OpIneJz>(),
        std::make_unique<OpIgtJz>(),
        std::make_unique<OpIgeJz>(),
        std::make_unique<OpIltJz>(),
        std::make_unique<OpIleJz>(),
        std::make_unique<OpCall>(),
        std::make_unique<OpStatic<OpU24>>(),
        std::make_unique<OpStaticLoad<OpU24>>(),
        std::make_unique<OpStaticStore<OpU24>>(),
        std::make_unique<OpGlobal<OpU24>>(),
        std::make_unique<OpGlobalLoad<OpU24>>(),
        std::make_unique<OpGlobalStore<OpU24>>(),
        std::make_unique<OpPushConstU24>(),
        std::make_unique<OpSwitch>(),
        std::make_unique<OpString>(),
        std::make_unique<OpStringhash>(),
        std::make_unique<OpTextLabelAssignString>(),
        std::make_unique<OpTextLabelAssignInt>(),
        std::make_unique<OpTextLabelAppendString>(),
        std::make_unique<OpTextLabelAppendInt>(),
        std::make_unique<OpTextLabelCopy>(),
        std::make_unique<OpCatch>(),
        std::make_unique<OpThrow>(),
        std::make_unique<OpCallindirect>(),
        std::make_unique<OpPushConst>(-1),
        std::make_unique<OpPushConst>(0),
        std::make_unique<OpPushConst>(1),
        std::make_unique<OpPushConst>(2),
        std::make_unique<OpPushConst>(3),
        std::make_unique<OpPushConst>(4),
        std::make_unique<OpPushConst>(5),
        std::make_unique<OpPushConst>(6),
        std::make_unique<OpPushConst>(7),
        std::make_unique<OpPushConstFloat>(-1.f),
        std::make_unique<OpPushConstFloat>(0.f),
        std::make_unique<OpPushConstFloat>(1.f),
        std::make_unique<OpPushConstFloat>(2.f),
        std::make_unique<OpPushConstFloat>(3.f),
        std::make_unique<OpPushConstFloat>(4.f),
        std::make_unique<OpPushConstFloat>(5.f),
        std::make_unique<OpPushConstFloat>(6.f),
        std::make_unique<OpPushConstFloat>(7.f),
        std::make_unique<OpIsBitSet>(),
    })
{
};
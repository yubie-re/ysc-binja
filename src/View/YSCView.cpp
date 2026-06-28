#include "inc.hpp"
#include "YSCView.hpp"
#include "Architecture/YSCArchitecture.hpp"
#include "Common/Env.hpp"
#include "Instructions/OperationEnum.hpp"
#include "Profiling/YSCTrace.hpp"
#include "Data/Crossmap.hpp"
#include "NativeMetadata.hpp"
#include "json/json.h"
#include <fstream>

using namespace nlohmann;

YSCView::~YSCView()
{
    YSC_TRACE_SCOPE("ysc.view", "YSCView::~YSCView");
    YSCTrace::MemorySnapshot("view.destroy.begin.rssKb");
    RetireYSCArchitectureView(m_archCacheKey);
    YSCTrace::MemorySnapshot("view.destroy.end.rssKb");
    YSCTrace::FlushThrottled();
}

bool YSCView::Init()
{
    YSC_TRACE_SCOPE("ysc.view", "YSCView::Init");
    m_archCacheKey = MarkYSCArchitectureViewActive(this);
    YSCTrace::MemorySnapshot("view.init.begin.rssKb");
    try
    {
        SetDefaultArchitecture(BinaryNinja::Architecture::GetByName("YSC"));
        SetDefaultPlatform(BinaryNinja::Architecture::GetByName("YSC")->GetStandalonePlatform());
        YSCHeader header {};
        GetParentView()->Read(&header, 0, sizeof(YSCHeader));

        uint32_t instructionOffset = CODE_OFFSET;
        uint32_t stringOffset = instructionOffset + header.m_codeSize;
        uint32_t staticOffset = stringOffset + header.m_stringHeapSize;
        uint32_t globalOffset = staticOffset + header.m_staticCount * 8;
        uint32_t nativeOffset = globalOffset + header.m_globalCount * 8;
        uint32_t memEnd = nativeOffset + header.m_nativesCount;
        (void) memEnd;

        YSCTrace::Counter("ysc.view", "codeSize", header.m_codeSize);
        YSCTrace::Counter("ysc.view", "stringHeapSize", header.m_stringHeapSize);
        YSCTrace::Counter("ysc.view", "staticCount", header.m_staticCount);
        YSCTrace::Counter("ysc.view", "globalCount", header.m_globalCount);
        YSCTrace::Counter("ysc.view", "nativeCount", header.m_nativesCount);
        size_t functionAnalysisLimit = YSCGetEnvSize("YSC_BINJA_FUNCTION_ANALYSIS_LIMIT", 2048);
        bool limitCodeScan = functionAnalysisLimit != 0 && header.m_codeSize > 1024 * 1024 &&
            !YSCGetEnvEnabled("YSC_BINJA_ENABLE_LARGE_SCRIPT_CODE_SCAN");
        uint32_t codeFlags = BNSegmentFlag::SegmentContainsCode | BNSegmentFlag::SegmentReadable;
        if (!limitCodeScan)
            codeFlags |= BNSegmentFlag::SegmentExecutable;
        YSCTrace::Counter("ysc.view", "codeScanExecutable", limitCodeScan ? 0 : 1);

        {
            YSC_TRACE_SCOPE("ysc.view", "Write CODE pages");
            WritePages(header.m_codeTable, header.m_codeSize, instructionOffset,
                codeFlags,
                "CODE", BNSectionSemantics::ReadOnlyCodeSectionSemantics);
        }
        AddEntryPointForAnalysis(GetDefaultPlatform(), instructionOffset);
        {
            YSC_TRACE_SCOPE("ysc.view", "Write STRINGS pages");
            WritePages(header.m_stringHeapTable, header.m_stringHeapSize, stringOffset,
                BNSegmentFlag::SegmentContainsData | BNSegmentFlag::SegmentReadable,
                "STRINGS", BNSectionSemantics::ReadOnlyDataSectionSemantics);
        }
        AddAutoSegment(staticOffset, 4 * header.m_staticCount, 
            *header.m_staticsTable, 4 * header.m_staticCount, BNSegmentFlag::SegmentContainsData | BNSegmentFlag::SegmentReadable);
        {
            YSC_TRACE_SCOPE_I("ysc.view", "Copy statics", "count", header.m_staticCount);
            for(uint32_t i = 0; i < header.m_staticCount; i++)
            {
                uint32_t staticVar = 0;
                GetParentView()->Read(&staticVar, *header.m_staticsTable + i * 8, 4);
                Write(staticOffset + 4 * i, &staticVar, sizeof(staticVar));
            }
        }

        AddAutoSection("STATICS", staticOffset, header.m_staticCount * sizeof(uint32_t), 
            BNSectionSemantics::ReadOnlyDataSectionSemantics);
        AddAutoSegment(nativeOffset, header.m_nativesCount * sizeof(uint64_t), 
            0, 0, 0);
        AddAutoSection("NATIVES", nativeOffset, header.m_nativesCount * sizeof(uint64_t), 
            BNSectionSemantics::ExternalSectionSemantics);
        // globalBlocks[0x12][0x40000]
        AddAutoSegment(0x60000000, 0x13 * 0x40000 * 4, 
            0, 0, 0);
        AddAutoSection("GLOBALS", 0x60000000, 0x13 * 0x40000 * 4, 
            BNSectionSemantics::ExternalSectionSemantics);
        
        json j;

        {
            YSC_TRACE_SCOPE("ysc.view", "Load natives.json");
            try
            {
                std::filesystem::path p = std::filesystem::path(BinaryNinja::GetUserPluginDirectory()) / "natives.json";
                std::ifstream ifs(p);
                ifs >> j;
            }
            catch(const std::exception& e)
            {
                std::cerr << e.what() << '\n';
            }
        }
        
        uint32_t parentNativeTablePtr = *header.m_nativesTable;
        {
            YSC_TRACE_SCOPE_I("ysc.view", "Define native symbols", "count", header.m_nativesCount);
            for(uint32_t i = 0; i < header.m_nativesCount; i++)
            {
                uint64_t native = 0 ;
                uint32_t nativeAddressParent = parentNativeTablePtr + i * sizeof(uint64_t);
                uint32_t nativeAddressVirtual = nativeOffset + i * sizeof(uint64_t);
                
                GetParentView()->Read(&native, nativeAddressParent, sizeof(uint64_t));
                native = RotLeft(native, i + header.m_codeSize);
                //Write(nativeAddress, &native, 8);
                if(!g_reverseCrossmap.contains(native))
                {
                    using namespace BinaryNinja;
                    Ref<Type> returnValue = Type::IntegerType(4, true);
                    Ref<CallingConvention> callConvention = GetDefaultArchitecture()->GetDefaultCallingConvention();
                    std::vector<FunctionParameter> params;
                    DefineDataVariable(nativeAddressVirtual, Type::FunctionType(returnValue, callConvention, params, false, 0));
                    DefineAutoSymbol(new Symbol(BNSymbolType::ExternalSymbol, fmt::format("native_{}", native), nativeAddressVirtual));
                    continue;
                }
                uint64_t nativeDay1 = g_reverseCrossmap.at(native);
                bool found = false;
                for (auto& namespce : j.items())
                {
                    auto find = FindNativeMetadata(namespce.value(), nativeDay1);
                    if(find != namespce.value().end())
                    {
                        found = true;
                        auto nativeStruct = *find;
                        using namespace BinaryNinja;
                        Ref<Type> returnValue = NativeJsonTypeToBN(GetDefaultArchitecture(), nativeStruct.value("return_type", "int"));
                        Ref<CallingConvention> callConvention = GetDefaultArchitecture()->GetDefaultCallingConvention();
                        std::vector<FunctionParameter> params;
                        for(auto& x : nativeStruct["params"])
                        {
                            params.push_back(FunctionParameter(x.value("name", "param"), NativeJsonTypeToBN(GetDefaultArchitecture(), x.value("type", "int"))));
                        }
                        DefineDataVariable(nativeAddressVirtual, Type::FunctionType(returnValue, callConvention, params, false, 0));
                        DefineAutoSymbol(new Symbol(BNSymbolType::ExternalSymbol,
                            NativeSymbolName(namespce.key(), nativeStruct.value("name", "UNKNOWN_NATIVE")),
                            nativeAddressVirtual));
                        break;
                    }
                }

                if(!found)
                {
                    using namespace BinaryNinja;
                    Ref<Type> returnValue = Type::IntegerType(4, true);
                    Ref<CallingConvention> callConvention = GetDefaultArchitecture()->GetDefaultCallingConvention();
                    std::vector<FunctionParameter> params;
                    DefineDataVariable(nativeAddressVirtual, Type::FunctionType(returnValue, callConvention, params, false, 0));
                    DefineAutoSymbol(new Symbol(BNSymbolType::ExternalSymbol, fmt::format("native_{}", native), nativeAddressVirtual));
                }
            }
        }

        size_t staticSymbolLimit = YSCGetEnvSize("YSC_BINJA_STATIC_SYMBOL_LIMIT", 4096);
        if (header.m_staticCount <= staticSymbolLimit)
        {
            YSC_TRACE_SCOPE_I("ysc.view", "Define static symbols", "count", header.m_staticCount);
            for(uint32_t i = 0; i < header.m_staticCount; i++)
            {
                DefineDataVariable(staticOffset + i * 4, BinaryNinja::Type::IntegerType(4, true));
                DefineAutoSymbol(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, fmt::format("Local_{}", i), staticOffset + i * 4));
            }
        }
        else
        {
            YSCTrace::InstantInt("ysc.view", "Skipped eager static symbols", "count", header.m_staticCount);
        }
        YSCTrace::MemorySnapshot("view.init.end.rssKb");
        YSCTrace::Flush();
    }
    catch(std::exception& ex)
    {
        BinaryNinja::LogError("Error loading YSC: %s", ex.what());
        return false;
    }
    return true;
}

BinaryNinja::Ref<BinaryNinja::BinaryView> YSCViewType::Create(BinaryNinja::BinaryView *data)
{
    return new YSCView(data);
}

bool YSCViewType::IsTypeValidForData (BinaryNinja::BinaryView *data)
{
    YSCHeader header;
    if(data->Read(&header, 0, sizeof(YSCHeader)) < sizeof(YSCHeader))
        return false;
    YSCPointer firstCodePage;
    if(data->Read(&firstCodePage, *header.m_codeTable, sizeof(YSCPointer)) != sizeof(YSCPointer))
        return false;
    uint8_t firstCodeByte = 0;
    if(data->Read(&firstCodeByte, *firstCodePage, 1) != 1)
        return false;
    if(firstCodeByte != OP_ENTER)
        return false;
    return true;
}

#include "inc.hpp"
#include "YSCView.hpp"
#include "Instructions/OperationEnum.hpp"
#include "Crossmap.hpp"
#include "json/json.h"
#include <fstream>

using namespace nlohmann;

#define PAGE_SIZE 0x4000

size_t YSCView::GetPageSize(size_t pageIndex, size_t pageCount, size_t totalSize) const
{
    if(pageIndex >= pageCount)
        return 0;
    if(pageIndex == pageCount - 1)
        return (totalSize % PAGE_SIZE) ? (totalSize % PAGE_SIZE) : PAGE_SIZE;
    return PAGE_SIZE;
}

uint64_t RotLeft(uint64_t value, uint64_t count)
{
    count &= 63;
    return (value << count) | (value >> (64 - count));
}

bool YSCView::Init()
{
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

        WritePages(header.m_codeTable, header.m_codeSize, instructionOffset, 
            BNSegmentFlag::SegmentContainsCode | BNSegmentFlag::SegmentExecutable | BNSegmentFlag::SegmentReadable ,
            "CODE", BNSectionSemantics::ReadOnlyCodeSectionSemantics);
        WritePages(header.m_stringHeapTable, header.m_stringHeapSize, stringOffset, 
            BNSegmentFlag::SegmentContainsData | BNSegmentFlag::SegmentReadable,
            "STRINGS", BNSectionSemantics::ReadOnlyDataSectionSemantics);
        AddAutoSegment(staticOffset, 4 * header.m_staticCount, 
            *header.m_staticsTable, 4 * header.m_staticCount, BNSegmentFlag::SegmentContainsData | BNSegmentFlag::SegmentReadable);
        for(uint32_t i = 0; i < header.m_staticCount; i++)
        {
            uint32_t staticVar = 0;
            GetParentView()->Read(&staticVar, *header.m_staticsTable + i * 8, 4);
            Write(staticOffset + 4 * i, &staticVar, sizeof(staticVar));
        }

        AddAutoSection("STATICS", staticOffset, header.m_staticCount * sizeof(uint32_t), 
            BNSectionSemantics::ReadOnlyDataSectionSemantics);
        AddAutoSegment(nativeOffset, header.m_nativesCount * sizeof(uint64_t), 
            0, 0, 0);
        AddAutoSection("NATIVES", nativeOffset, header.m_nativesCount * sizeof(uint64_t), 
            BNSectionSemantics::ExternalSectionSemantics);
        // globalBlocks[0x12][0x40000]
        AddAutoSegment(0x60000000, 0x13 * 0x40000, 
            0, 0, 0);
        AddAutoSection("GLOBALS", 0x60000000, 0x13 * 0x40000, 
            BNSectionSemantics::ExternalSectionSemantics);
        
        std::filesystem::path p = std::filesystem::path(BinaryNinja::GetUserPluginDirectory()) / "natives.json";
        json j;
        std::ifstream ifs(p);
        ifs >> j;
        uint32_t parentNativeTablePtr = *header.m_nativesTable;
        for(int i = 0; i < header.m_nativesCount; i++)
        {
            uint64_t native = 0 ;
            uint32_t nativeAddressParent = parentNativeTablePtr + i * sizeof(uint64_t);
            uint32_t nativeAddressVirtual = nativeOffset + i * sizeof(uint64_t);
            
            GetParentView()->Read(&native, nativeAddressParent, sizeof(uint64_t));
            native = RotLeft(native, i + header.m_codeSize);
            //Write(nativeAddress, &native, 8);
            if(!g_reverseCrossmap.contains(native))
                continue;
            uint64_t nativeDay1 = g_reverseCrossmap.at(native);
            auto jsonHash = fmt::format("0x{:X}", nativeDay1);
            bool found = false;
            for (auto& namespce : j.items())
            {
                auto find = namespce.value().find(jsonHash);
                if(find != namespce.value().end())
                {
                    found = true;
                    auto nativeStruct = *find;
                    using namespace BinaryNinja;
                    Ref<Type> returnValue = Type::IntegerType(4, true);
                    Ref<CallingConvention> callConvention = GetDefaultArchitecture()->GetDefaultCallingConvention();
                    std::vector<FunctionParameter> params;
                    for(auto& x : nativeStruct["params"])
                    {
                        params.push_back(FunctionParameter(x["name"].get<std::string>(), Type::IntegerType(4, true)));
                    }
                    DefineDataVariable(nativeAddressVirtual, Type::FunctionType(returnValue, callConvention, params, false, 0));
                    DefineAutoSymbol(new Symbol(BNSymbolType::ExternalSymbol, fmt::format("native_{}_{}", namespce.key(), nativeStruct["name"].get<std::string>()), nativeAddressVirtual));
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

        for(int i = 0; i < header.m_staticCount; i++)
        {
            DefineDataVariable(staticOffset + i * 4, BinaryNinja::Type::IntegerType(4, true));
            DefineAutoSymbol(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, fmt::format("Local_{}", i), staticOffset + i * 4));
        }
    }
    catch(std::exception& ex)
    {
        BinaryNinja::LogError("Error loading YSC: %s", ex.what());
        return false;
    }
    return true;
}

void YSCView::WritePages(YSCPointer tablePtr, uint32_t totalSize, uint32_t virtualAddress,
    uint32_t flags, std::string_view name, BNSectionSemantics semantics)
{
    uint32_t pageCount = (totalSize + PAGE_SIZE - 1) / PAGE_SIZE;
    std::vector<YSCPointer> tableEntries(pageCount);

    GetParentView()->Read(tableEntries.data(), *tablePtr, pageCount * sizeof(YSCPointer));

    for(uint32_t i = 0, offset = virtualAddress; i < pageCount; i++)
    {
        uint32_t pageSize = GetPageSize(i, pageCount, totalSize);
        uint32_t pageFileAddress = *tableEntries[i];
        uint32_t pageVirtualAddress = offset;
        //std::vector<uint8_t> page(pageSize);
        AddAutoSegment(offset, pageSize, pageFileAddress, pageSize, flags);
        offset += pageSize;
    }
    AddAutoSection(std::string(name), virtualAddress, totalSize, semantics);
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
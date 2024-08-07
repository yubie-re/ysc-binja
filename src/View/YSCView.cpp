#include "inc.hpp"
#include "YSCView.hpp"
#include "Instructions/OperationEnum.hpp"

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

        uint32_t instructionOffset = 0;
        uint32_t stringOffset = instructionOffset + header.m_codeSize;
        uint32_t staticOffset = stringOffset + header.m_stringHeapSize;
        uint32_t globalOffset = staticOffset + header.m_staticCount;
        uint32_t nativeOffset = globalOffset + header.m_globalCount;
        uint32_t memEnd = nativeOffset + header.m_nativesCount;

        WritePages(header.m_codeTable, header.m_codeSize, instructionOffset, 
            BNSegmentFlag::SegmentContainsCode | BNSegmentFlag::SegmentExecutable | BNSegmentFlag::SegmentReadable ,
            "CODE", BNSectionSemantics::ReadOnlyCodeSectionSemantics);
        WritePages(header.m_stringHeapTable, header.m_stringHeapSize, stringOffset, 
            BNSegmentFlag::SegmentContainsData | BNSegmentFlag::SegmentReadable,
            "STRINGS", BNSectionSemantics::ReadOnlyDataSectionSemantics);

        AddAutoSegment(nativeOffset, header.m_nativesCount * sizeof(uint64_t), 
            *header.m_nativesTable, header.m_nativesCount * 8, BNSegmentFlag::SegmentContainsData | BNSegmentFlag::SegmentReadable);
        AddAutoSection("NATIVES", nativeOffset, header.m_nativesCount * sizeof(uint64_t), 
            BNSectionSemantics::ReadOnlyDataSectionSemantics);

        for(int i = 0; i < header.m_nativesCount; i++)
        {
            uint64_t native = 0 ;
            uint32_t nativeAddress = nativeOffset + i * sizeof(uint64_t);

            Read(&native, nativeAddress, sizeof(uint64_t));
            native = RotLeft(native, i + *header.m_nativesTable);
            Write(nativeAddress, &native, 8);
            DefineDataVariable(nativeAddress, BinaryNinja::Type::IntegerType(8, false));
            DefineAutoSymbol(BinaryNinja::Ref<BinaryNinja::Symbol>(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, fmt::format("native_{:X}", native), nativeAddress)));
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
#include "inc.hpp"
#include "View/YSCView.hpp"

size_t YSCView::GetPageSize(size_t pageIndex, size_t pageCount, size_t totalSize) const
{
    if(pageIndex >= pageCount)
        return 0;
    if(pageIndex == pageCount - 1)
        return (totalSize % YSC_PAGE_SIZE) ? (totalSize % YSC_PAGE_SIZE) : YSC_PAGE_SIZE;
    return YSC_PAGE_SIZE;
}

void YSCView::WritePages(YSCPointer tablePtr, uint32_t totalSize, uint32_t virtualAddress,
    uint32_t flags, std::string_view name, BNSectionSemantics semantics)
{
    uint32_t pageCount = (totalSize + YSC_PAGE_SIZE - 1) / YSC_PAGE_SIZE;
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

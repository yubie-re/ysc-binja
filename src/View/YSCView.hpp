#ifndef YSC_VIEW_HPP
#define YSC_VIEW_HPP

#include "YSCFormat.hpp"

class YSCView : public BinaryNinja::BinaryView
{
public:
    YSCView(BinaryView* data) : BinaryView("YSC", data->GetFile(), data), m_parent(data) {};
    ~YSCView() override;
    bool Init() override;
    uint64_t PerformGetEntryPoint() const override { return CODE_OFFSET; }
    bool PerformIsExecutable() const override { return true; }
private:
    BinaryView* m_parent;
    uintptr_t m_archCacheKey = 0;
    size_t GetPageSize(size_t pageIndex, size_t pageCount, size_t totalSize) const;
    void WritePages(YSCPointer tablePtr, uint32_t tableSize, uint32_t virtualAddress,
        uint32_t flags, std::string_view name, BNSectionSemantics semantics = BNSectionSemantics::DefaultSectionSemantics);
};

class YSCViewType : public BinaryNinja::BinaryViewType
{
public:
    YSCViewType() : BinaryViewType("YSC", "GTA 5 YSC SCRIPT CONTAINER") {}
    BinaryNinja::Ref<BinaryNinja::BinaryView> Create(BinaryNinja::BinaryView *data) override;
    bool IsTypeValidForData (BinaryNinja::BinaryView *data) override;
};

#endif

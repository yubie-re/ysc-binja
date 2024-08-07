#ifndef YSC_VIEW_HPP
#define YSC_VIEW_HPP

struct YSCPointer
{
    uint64_t m_ptr;

    uint32_t operator* ()
    {
        return m_ptr & 0xFFFFFF;
    }
};

struct YSCHeader
{
	uint64_t m_pageBase;// 0x00
	uint64_t m_pageMapPointer;// 0x08
	YSCPointer m_codeTable;// 0x10 Points to an array of code block offsets
	uint32_t m_globalsSignature;// 0x18
	uint32_t m_codeSize;// 0x1C - The size of all the code tables
	uint32_t m_parameterCount;// 0x20 - These are for starting a script with args. The args appear at the start of the script static variables
	uint32_t m_staticCount;// 0x24 - The number of static variables in the script
	uint32_t m_globalCount;// 0x28 - This is used for scripts that seem to initialise global variable tables
	uint32_t m_nativesCount;// 0x2C - The total amount of natives in the native table
	YSCPointer m_staticsTable;// 0x30 - The Offset in file where static variables are initialised
	YSCPointer m_globalsTable;// 0x38 - The Offset in file where global variales are initilaised(only used for registration scripts)
	YSCPointer m_nativesTable;// 0x40 - The Offset in file where the natives table is stored
	uint64_t m_null1;//0x48
	uint64_t m_null2;//0x50;
	uint32_t m_scriptNameHash;//0x58 - A Jenkins hash of the scripts name
	uint32_t m_unkUsually1;//0x5C
	YSCPointer m_scriptNamePointer;//0x60 - Points to an offset in the file that has the name of the script
	YSCPointer m_stringHeapTable;//0x68 - Points to an array of string block offsets
	uint32_t m_stringHeapSize;//0x70 - The Size of all the string tables
	uint32_t m_Null3;//0x74
	uint32_t m_Null4;//0x78
	uint32_t m_Null5;//0x7C
};
static_assert(sizeof(YSCHeader) == 0x80);

struct YSCPageEntry
{

};

class YSCView : public BinaryNinja::BinaryView
{
public:
    YSCView(BinaryView* data) : BinaryView("YSC", data->GetFile(), data), m_parent(data) {};
    bool Init() override;
    uint64_t PerformGetEntryPoint() const override { return 0; }
    bool PerformIsExecutable() const override { return true; }
private:
    BinaryView* m_parent;
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
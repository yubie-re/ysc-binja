#ifndef CC_HPP
#define CC_HPP

class YSCCallingConvention: public BinaryNinja::CallingConvention
{
public:
	YSCCallingConvention(BinaryNinja::Architecture* arch): BinaryNinja::CallingConvention(arch, "sccall")
	{
	}

    virtual bool 	IsStackReservedForArgumentRegisters () override
    {
        return false;
    }

    virtual bool 	IsStackAdjustedOnReturn () override
    {
        return true;
    }

    virtual bool 	IsEligibleForHeuristics () override
    {
        return true;
    }

	virtual std::vector<uint32_t> GetCallerSavedRegisters() override
	{
		return std::vector<uint32_t>{ Reg_SP };
	}

	virtual std::vector<uint32_t> GetCalleeSavedRegisters() override
	{
		return std::vector<uint32_t>{ Reg_SP };
	}

	virtual uint32_t GetIntegerReturnValueRegister() override
	{
		return Reg_SP;
	}
};


#endif CC_HPP
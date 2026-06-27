#ifndef CC_HPP
#define CC_HPP

#include "../Architecture/YSCArchitecture.hpp"

class YSCCallingConvention: public BinaryNinja::CallingConvention
{
private:
    static bool IsArgRegister(uint64_t reg)
    {
        return reg >= Reg_ARG0 && reg <= Reg_ARG15;
    }

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
		return std::vector<uint32_t>{ Reg_R1, Reg_R2, Reg_R3, Reg_R4, Reg_SWITCH, Reg_VX1, Reg_VY1, Reg_VZ1, Reg_VX2, Reg_VY2, Reg_VZ2,
            Reg_ARG0, Reg_ARG1, Reg_ARG2, Reg_ARG3, Reg_ARG4, Reg_ARG5, Reg_ARG6, Reg_ARG7,
            Reg_ARG8, Reg_ARG9, Reg_ARG10, Reg_ARG11, Reg_ARG12, Reg_ARG13, Reg_ARG14, Reg_ARG15 };
	}

    virtual std::vector<uint32_t> GetIntegerArgumentRegisters() override
    {
        return std::vector<uint32_t>{ Reg_ARG0, Reg_ARG1, Reg_ARG2, Reg_ARG3, Reg_ARG4, Reg_ARG5, Reg_ARG6, Reg_ARG7,
            Reg_ARG8, Reg_ARG9, Reg_ARG10, Reg_ARG11, Reg_ARG12, Reg_ARG13, Reg_ARG14, Reg_ARG15 };
    }

    virtual std::vector<uint32_t> GetFloatArgumentRegisters() override
    {
        return std::vector<uint32_t>{ Reg_ARG0, Reg_ARG1, Reg_ARG2, Reg_ARG3, Reg_ARG4, Reg_ARG5, Reg_ARG6, Reg_ARG7,
            Reg_ARG8, Reg_ARG9, Reg_ARG10, Reg_ARG11, Reg_ARG12, Reg_ARG13, Reg_ARG14, Reg_ARG15 };
    }

    virtual bool AreArgumentRegistersSharedIndex() override
    {
        return true;
    }

	virtual std::vector<uint32_t> GetCalleeSavedRegisters() override
	{
		return std::vector<uint32_t>{ Reg_FP, Reg_SP };
	}

	virtual uint32_t GetIntegerReturnValueRegister() override
	{
		return Reg_R1;
	}

    virtual BinaryNinja::Variable GetIncomingVariableForParameterVariable(const BinaryNinja::Variable& var,
                                                                          BinaryNinja::Function* func) override
    {
        if (var.type == RegisterVariableSourceType && IsArgRegister(var.storage))
            return var;
        return BinaryNinja::CallingConvention::GetIncomingVariableForParameterVariable(var, func);
    }

    virtual BinaryNinja::Variable GetParameterVariableForIncomingVariable(const BinaryNinja::Variable& var,
                                                                          BinaryNinja::Function* func) override
    {
        if (var.type == RegisterVariableSourceType && IsArgRegister(var.storage))
            return var;
        return BinaryNinja::CallingConvention::GetParameterVariableForIncomingVariable(var, func);
    }
};


#endif // CC_HPP

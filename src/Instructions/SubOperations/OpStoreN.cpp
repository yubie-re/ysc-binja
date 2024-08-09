#include "inc.hpp"
#include "OpStoreN.hpp"
#include "Architecture/YSCArchitecture.hpp"
#include "lowlevelilinstruction.h"

size_t OpStoreN::GetSize()
{
    return 1;
}

std::string_view OpStoreN::GetName()
{
    return "STORE_N";
}

bool OpStoreN::GetInstructionLowLevelIL(const uint8_t* data, uint64_t addr, size_t& len, BinaryNinja::LowLevelILFunction& il)
{
    il.AddInstruction(il.SetRegister(4, Reg_POPHOLDER, il.Pop(4))); // Address
    il.AddInstruction(il.SetRegister(4, Reg_I, il.Pop(4))); // Count
    il.AddInstruction(il.SetRegister(4, Reg_TMP, il.Register(4, Reg_I))); // Count
    std::vector<BinaryNinja::ExprId> params = {il.Register(4, Reg_POPHOLDER), il.Register(4, Reg_I)};
    std::vector<BinaryNinja::RegisterOrFlag > outputs = {};
    il.AddInstruction(il.Intrinsic(outputs, Intrin_STOREN, params));
    il.AddInstruction(
        il.SetRegister(4, Reg_SP,
            il.Sub(4, 
                il.Register(4, Reg_SP), 
                il.Mult(4, 
                    il.Const(4, 4), 
                    il.Register(4, Reg_TMP)
                )
            )
        )
    );
    return true;
}

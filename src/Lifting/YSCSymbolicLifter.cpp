#include "inc.hpp"
#include "Lifting/YSCSymbolicLifter.hpp"
#include "Architecture/FunctionFacts.hpp"
#include "Architecture/YSCArchitecture.hpp"
#include "Common/Env.hpp"
#include "Instructions/OperationEnum.hpp"
#include "Lifting/LiftingSupport.hpp"
#include "lowlevelilinstruction.h"
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>

namespace
{
constexpr uint32_t YSC_LOCAL_TEMP_BASE = 0x10000;
constexpr uint32_t YSC_STACK_TEMP_BASE = 0x20000;
constexpr uint32_t YSC_CALL_RESULT_TEMP_BASE = 0x80000;
constexpr size_t YSC_MAX_RUNTIME_ARRAY_OFFSET_ALIASES_PER_BASE = 16;
constexpr bool YSC_ENABLE_RUNTIME_ARRAY_METADATA_BY_DEFAULT = true;

bool YSCRuntimeArrayMetadataEnabled()
{
    return YSCGetEnvEnabled("YSC_BINJA_RUNTIME_ARRAY_METADATA", YSC_ENABLE_RUNTIME_ARRAY_METADATA_BY_DEFAULT);
}

template <typename T>
T ReadUnaligned(const uint8_t* data)
{
    T result {};
    std::memcpy(&result, data, sizeof(T));
    return result;
}

uint64_t BranchTarget(uint64_t addr, const uint8_t* opcode)
{
    return static_cast<uint64_t>(static_cast<int64_t>(addr) + static_cast<int16_t>(ReadUnaligned<int16_t>(opcode + 1)) + 3);
}

uint32_t DecodeU24(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[1]) << 8) | data[0];
}

uint32_t LocalTemp(uint32_t index)
{
    return LLIL_TEMP(YSC_LOCAL_TEMP_BASE + index);
}

uint32_t StackTemp(uint32_t blockIndex, uint32_t index)
{
    return LLIL_TEMP(YSC_STACK_TEMP_BASE + blockIndex * 256 + index);
}

uint32_t CallResultTemp(uint64_t address)
{
    return LLIL_TEMP(YSC_CALL_RESULT_TEMP_BASE + static_cast<uint32_t>(address & 0xffff));
}

uint32_t ArgReg(uint32_t index)
{
    return index < 16 ? Reg_ARG0 + index : Reg_ARG15;
}

uint32_t ReturnReg(uint32_t index)
{
    return index < 4 ? Reg_R1 + index : Reg_R4;
}

BinaryNinja::Ref<BinaryNinja::Type> VolatileInt32Type()
{
    BinaryNinja::TypeBuilder builder(BinaryNinja::Type::IntegerType(4, true));
    builder.SetVolatile(BinaryNinja::Confidence<bool>(true));
    return builder.Finalize();
}

void DefineAutoInt32DataSymbol(BinaryNinja::BinaryView* view, uint64_t address, const std::string& name)
{
    if (!view)
        return;

    BinaryNinja::DataVariable existingVariable {};
    if (!view->GetDataVariableAtAddress(address, existingVariable))
        view->DefineDataVariable(address, VolatileInt32Type());

    if (view->GetSymbolByAddress(address))
        return;

    view->DefineAutoSymbol(new BinaryNinja::Symbol(BNSymbolType::DataSymbol, name, address));
}
}

class YSCSymbolicLifter::Impl
{
  public:
    Impl(YSCArchitecture* arch, BinaryNinja::LowLevelILFunction& il, BinaryNinja::BinaryView* view) :
        m_arch(arch), m_il(il), m_view(view)
    {}

    bool Lift(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (!m_symbolic)
            return false;
        if (len < 1 || opcode[0] >= OP_MAX)
            return false;

        m_il.SetCurrentAddress(m_arch, addr);
        const uint8_t* data = opcode + 1;
        switch (opcode[0])
        {
        case OP_NOP:
            m_il.AddInstruction(m_il.Nop());
            len = 1;
            return true;
        case OP_ENTER:
            return Enter(opcode, len);
        case OP_PUSH_CONST_M1: Push(m_il.Const(4, static_cast<uint32_t>(-1)), static_cast<uint32_t>(-1)); len = 1; return true;
        case OP_PUSH_CONST_0: case OP_PUSH_CONST_1: case OP_PUSH_CONST_2: case OP_PUSH_CONST_3:
        case OP_PUSH_CONST_4: case OP_PUSH_CONST_5: case OP_PUSH_CONST_6: case OP_PUSH_CONST_7:
            Push(m_il.Const(4, opcode[0] - OP_PUSH_CONST_0), opcode[0] - OP_PUSH_CONST_0); len = 1; return true;
        case OP_PUSH_CONST_FM1: Push(m_il.FloatConstSingle(-1.0f)); len = 1; return true;
        case OP_PUSH_CONST_F0: case OP_PUSH_CONST_F1: case OP_PUSH_CONST_F2: case OP_PUSH_CONST_F3:
        case OP_PUSH_CONST_F4: case OP_PUSH_CONST_F5: case OP_PUSH_CONST_F6: case OP_PUSH_CONST_F7:
            Push(m_il.FloatConstSingle(static_cast<float>(opcode[0] - OP_PUSH_CONST_F0))); len = 1; return true;
        case OP_PUSH_CONST_U8: if (len < 2) return false; Push(m_il.Const(4, data[0]), data[0]); len = 2; return true;
        case OP_PUSH_CONST_U8_U8: if (len < 3) return false; Push(m_il.Const(4, data[0]), data[0]); Push(m_il.Const(4, data[1]), data[1]); len = 3; return true;
        case OP_PUSH_CONST_U8_U8_U8: if (len < 4) return false; Push(m_il.Const(4, data[0]), data[0]); Push(m_il.Const(4, data[1]), data[1]); Push(m_il.Const(4, data[2]), data[2]); len = 4; return true;
        case OP_PUSH_CONST_S16: if (len < 3) return false; { uint32_t value = static_cast<uint32_t>(ReadUnaligned<int16_t>(data)); Push(m_il.Const(4, value), value); } len = 3; return true;
        case OP_PUSH_CONST_U24: if (len < 4) return false; { uint32_t value = DecodeU24(data); Push(m_il.Const(4, value), value); } len = 4; return true;
        case OP_PUSH_CONST_U32: if (len < 5) return false; { uint32_t value = ReadUnaligned<uint32_t>(data); Push(m_il.Const(4, value), value); } len = 5; return true;
        case OP_PUSH_CONST_F: if (len < 5) return false; Push(m_il.FloatConstSingle(ReadUnaligned<float>(data))); len = 5; return true;
        case OP_DUP: return Dup(len);
        case OP_DROP: return Drop(len);
        case OP_IADD: return PointerAdd(len);
        case OP_ISUB: return PointerSub(len);
        case OP_IMUL: return Binary(len, &BinaryNinja::LowLevelILFunction::Mult);
        case OP_IDIV: return Binary(len, &BinaryNinja::LowLevelILFunction::DivSigned);
        case OP_IMOD: return Binary(len, &BinaryNinja::LowLevelILFunction::ModSigned);
        case OP_IAND: return Binary(len, &BinaryNinja::LowLevelILFunction::And);
        case OP_IOR: return Binary(len, &BinaryNinja::LowLevelILFunction::Or);
        case OP_IXOR: return Binary(len, &BinaryNinja::LowLevelILFunction::Xor);
        case OP_INEG: return Unary(len, &BinaryNinja::LowLevelILFunction::Neg);
        case OP_INOT: return UnaryCustom(len, [&](BinaryNinja::ExprId v) { return m_il.CompareEqual(4, v, m_il.Const(4, 0)); });
        case OP_IEQ: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareEqual);
        case OP_INE: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareNotEqual);
        case OP_IGT: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareSignedGreaterThan);
        case OP_IGE: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareSignedGreaterEqual);
        case OP_ILT: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareSignedLessThan);
        case OP_ILE: return Compare(len, &BinaryNinja::LowLevelILFunction::CompareSignedLessEqual);
        case OP_FADD: return Binary(len, &BinaryNinja::LowLevelILFunction::FloatAdd);
        case OP_FSUB: return Binary(len, &BinaryNinja::LowLevelILFunction::FloatSub);
        case OP_FMUL: return Binary(len, &BinaryNinja::LowLevelILFunction::FloatMult);
        case OP_FDIV: return Binary(len, &BinaryNinja::LowLevelILFunction::FloatDiv);
        case OP_FMOD: return Fmod(len);
        case OP_FNEG: return Unary(len, &BinaryNinja::LowLevelILFunction::FloatNeg);
        case OP_FEQ: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareEqual);
        case OP_FNE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareNotEqual);
        case OP_FGT: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareGreaterThan);
        case OP_FGE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareGreaterEqual);
        case OP_FLT: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareLessThan);
        case OP_FLE: return Compare(len, &BinaryNinja::LowLevelILFunction::FloatCompareLessEqual);
        case OP_I2F: return Unary(len, &BinaryNinja::LowLevelILFunction::IntToFloat);
        case OP_F2I: return Unary(len, &BinaryNinja::LowLevelILFunction::FloatToInt);
        case OP_F2V: return F2V(len);
        case OP_VADD: return VectorBinary(len, &BinaryNinja::LowLevelILFunction::FloatAdd);
        case OP_VSUB: return VectorBinary(len, &BinaryNinja::LowLevelILFunction::FloatSub);
        case OP_VMUL: return VectorBinary(len, &BinaryNinja::LowLevelILFunction::FloatMult);
        case OP_VDIV: return VectorBinary(len, &BinaryNinja::LowLevelILFunction::FloatDiv);
        case OP_VNEG: return VectorUnary(len, &BinaryNinja::LowLevelILFunction::FloatNeg);
        case OP_IADD_U8: if (len < 2) return false; return PointerImmAdd(len, data[0], 2);
        case OP_IMUL_U8: if (len < 2) return false; return ImmBinary(len, data[0], &BinaryNinja::LowLevelILFunction::Mult);
        case OP_IADD_S16: if (len < 3) return false; return PointerImmAdd(len, ReadUnaligned<int16_t>(data), 3);
        case OP_IMUL_S16: if (len < 3) return false; return ImmBinary(len, static_cast<uint32_t>(ReadUnaligned<int16_t>(data)), &BinaryNinja::LowLevelILFunction::Mult, 3);
        case OP_IOFFSET: return IOffset(len);
        case OP_IOFFSET_U8: if (len < 2) return false; return IOffsetImm(len, data[0], 2);
        case OP_IOFFSET_S16: if (len < 3) return false; return IOffsetImm(len, ReadUnaligned<int16_t>(data), 3);
        case OP_IOFFSET_U8_LOAD: if (len < 2) return false; return IOffsetLoadImm(len, data[0], 2);
        case OP_IOFFSET_S16_LOAD: if (len < 3) return false; return IOffsetLoadImm(len, ReadUnaligned<int16_t>(data), 3);
        case OP_IOFFSET_U8_STORE: if (len < 2) return false; return IOffsetStoreImm(len, data[0], 2);
        case OP_IOFFSET_S16_STORE: if (len < 3) return false; return IOffsetStoreImm(len, ReadUnaligned<int16_t>(data), 3);
        case OP_LOCAL_U8: if (len < 2) return false; PushLocalAddr(data[0]); len = 2; return true;
        case OP_LOCAL_U16: if (len < 3) return false; PushLocalAddr(ReadUnaligned<uint16_t>(data)); len = 3; return true;
        case OP_LOCAL_U8_LOAD: if (len < 2) return false; Push(m_il.Register(4, LocalTemp(data[0]))); len = 2; return true;
        case OP_LOCAL_U16_LOAD: if (len < 3) return false; Push(m_il.Register(4, LocalTemp(ReadUnaligned<uint16_t>(data)))); len = 3; return true;
        case OP_LOCAL_U8_STORE: if (len < 2) return false; return StoreLocal(data[0], len, 2);
        case OP_LOCAL_U16_STORE: if (len < 3) return false; return StoreLocal(ReadUnaligned<uint16_t>(data), len, 3);
        case OP_STATIC_U8: if (len < 2) return false; PushAddress(StaticAddress(data[0])); len = 2; return true;
        case OP_STATIC_U16: if (len < 3) return false; PushAddress(StaticAddress(ReadUnaligned<uint16_t>(data))); len = 3; return true;
        case OP_STATIC_U24: if (len < 4) return false; PushAddress(StaticAddress(DecodeU24(data))); len = 4; return true;
        case OP_STATIC_U8_LOAD: if (len < 2) return false; Push(m_il.Load(4, m_il.ConstPointer(4, StaticAddress(data[0])))); len = 2; return true;
        case OP_STATIC_U16_LOAD: if (len < 3) return false; Push(m_il.Load(4, m_il.ConstPointer(4, StaticAddress(ReadUnaligned<uint16_t>(data))))); len = 3; return true;
        case OP_STATIC_U24_LOAD: if (len < 4) return false; Push(m_il.Load(4, m_il.ConstPointer(4, StaticAddress(DecodeU24(data))))); len = 4; return true;
        case OP_STATIC_U8_STORE: if (len < 2) return false; return StoreAddress(StaticAddress(data[0]), len, 2);
        case OP_STATIC_U16_STORE: if (len < 3) return false; return StoreAddress(StaticAddress(ReadUnaligned<uint16_t>(data)), len, 3);
        case OP_STATIC_U24_STORE: if (len < 4) return false; return StoreAddress(StaticAddress(DecodeU24(data)), len, 4);
        case OP_GLOBAL_U16: if (len < 3) return false; PushAddress(GlobalAddress(ReadUnaligned<uint16_t>(data))); len = 3; return true;
        case OP_GLOBAL_U24: if (len < 4) return false; PushAddress(GlobalAddress(DecodeU24(data))); len = 4; return true;
        case OP_GLOBAL_U16_LOAD: if (len < 3) return false; Push(m_il.Load(4, m_il.ConstPointer(4, GlobalAddress(ReadUnaligned<uint16_t>(data))))); len = 3; return true;
        case OP_GLOBAL_U24_LOAD: if (len < 4) return false; Push(m_il.Load(4, m_il.ConstPointer(4, GlobalAddress(DecodeU24(data))))); len = 4; return true;
        case OP_GLOBAL_U16_STORE: if (len < 3) return false; return StoreAddress(GlobalAddress(ReadUnaligned<uint16_t>(data)), len, 3);
        case OP_GLOBAL_U24_STORE: if (len < 4) return false; return StoreAddress(GlobalAddress(DecodeU24(data)), len, 4);
        case OP_LOAD: return Load(len);
        case OP_LOAD_N: return LoadN(len);
        case OP_STORE: return Store(len, false);
        case OP_STORE_REV: return Store(len, true);
        case OP_STORE_N: return StoreN(len);
        case OP_ARRAY_U8: if (len < 2) return false; return Array(data[0], len, 2);
        case OP_ARRAY_U16: if (len < 3) return false; return Array(ReadUnaligned<uint16_t>(data), len, 3);
        case OP_ARRAY_U8_LOAD: if (len < 2) return false; return ArrayLoad(data[0], len, 2);
        case OP_ARRAY_U16_LOAD: if (len < 3) return false; return ArrayLoad(ReadUnaligned<uint16_t>(data), len, 3);
        case OP_ARRAY_U8_STORE: if (len < 2) return false; return ArrayStore(data[0], len, 2);
        case OP_ARRAY_U16_STORE: if (len < 3) return false; return ArrayStore(ReadUnaligned<uint16_t>(data), len, 3);
        case OP_STRING: return String(len);
        case OP_STRINGHASH: return StringHash(len);
        case OP_IS_BIT_SET: return IsBitSet(len);
        case OP_TEXT_LABEL_ASSIGN_STRING:
            if (len < 2) return false;
            return TextLabelAssignString(data[0], len);
        case OP_TEXT_LABEL_ASSIGN_INT:
            if (len < 2) return false;
            return TextLabelAssignInt(data[0], len);
        case OP_TEXT_LABEL_APPEND_STRING:
            if (len < 2) return false;
            return TextLabelAppendString(data[0], len);
        case OP_TEXT_LABEL_APPEND_INT:
            if (len < 2) return false;
            return TextLabelAppendInt(data[0], len);
        case OP_TEXT_LABEL_COPY:
            return TextLabelCopy(len);
        case OP_J:
            return Jump(opcode, addr, len);
        case OP_JZ:
            return Jz(opcode, addr, len);
        case OP_IEQ_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareEqual);
        case OP_INE_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareNotEqual);
        case OP_IGT_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareSignedGreaterThan);
        case OP_IGE_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareSignedGreaterEqual);
        case OP_ILT_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareSignedLessThan);
        case OP_ILE_JZ:
            return CompareJz(opcode, addr, len, &BinaryNinja::LowLevelILFunction::CompareSignedLessEqual);
        case OP_SWITCH:
            return Switch(opcode, addr, len);
        case OP_NATIVE: return Native(opcode, addr, len);
        case OP_CALL: return Call(opcode, addr, len);
        case OP_LEAVE: return Leave(opcode, len);
        case OP_CATCH: Push(m_il.Const(4, static_cast<uint32_t>(-1)), static_cast<uint32_t>(-1)); len = 1; return true;
        case OP_THROW: m_il.AddInstruction(m_il.NoReturn()); len = 1; return true;
        case OP_CALLINDIRECT: return CallIndirect(len);
        default:
            return false;
        }
    }

    void DisableAndFlush()
    {
        for (auto& value : m_stack)
            m_il.AddInstruction(m_il.Push(4, value.expr));
        m_stack.clear();
        m_symbolic = false;
    }

    void Reset()
    {
        m_stack.clear();
        m_symbolic = true;
        m_syntheticInputCount = 0;
        m_expectedOutgoingStackDepth.reset();
        m_expectedOutgoingBlockIndices.clear();
        m_expectedFallthroughBlockIndex.reset();
        m_expectedBranchBlockIndex.reset();
        m_explicitFallthroughGoto = false;
    }

    void SetExpectedOutgoingStackDepth(std::optional<size_t> depth, std::vector<uint32_t> targetBlockIndices)
    {
        m_expectedOutgoingStackDepth = depth;
        m_expectedOutgoingBlockIndices = std::move(targetBlockIndices);
        m_expectedBranchBlockIndex.reset();
        m_expectedFallthroughBlockIndex.reset();
        m_explicitFallthroughGoto = false;
    }

    void SetExpectedBranchStackTargets(std::optional<size_t> depth, std::optional<uint32_t> fallthroughBlockIndex,
                                       std::optional<uint32_t> branchBlockIndex, bool explicitFallthroughGoto)
    {
        m_expectedOutgoingStackDepth = depth;
        m_expectedOutgoingBlockIndices.clear();
        m_expectedFallthroughBlockIndex = fallthroughBlockIndex;
        m_expectedBranchBlockIndex = branchBlockIndex;
        m_explicitFallthroughGoto = explicitFallthroughGoto;
    }

    void SeedStack(uint32_t blockIndex, size_t depth)
    {
        m_stack.clear();
        m_symbolic = true;
        m_currentBlockIndex = blockIndex;
        m_seededStackDepth = depth;
        m_syntheticInputCount = static_cast<uint32_t>(depth);
        for (size_t i = 0; i < depth; i++)
            Push(m_il.Register(4, StackTemp(blockIndex, static_cast<uint32_t>(i))));
    }

    void SetValidInstructionStarts(std::unordered_set<uint64_t> instructionStarts)
    {
        m_validInstructionStarts = std::move(instructionStarts);
    }

    bool StoreStackOutputs(uint32_t targetBlockIndex, size_t expectedDepth)
    {
        if (m_stack.size() != expectedDepth)
            return false;
        for (size_t i = 0; i < m_stack.size(); i++)
            m_il.AddInstruction(m_il.SetRegister(4, StackTemp(targetBlockIndex, static_cast<uint32_t>(i)), m_stack[i].expr));
        return true;
    }

    bool StoreStackOutputs(size_t expectedDepth)
    {
        if (m_expectedOutgoingBlockIndices.empty())
            return false;
        for (auto targetBlockIndex : m_expectedOutgoingBlockIndices)
        {
            if (!StoreStackOutputs(targetBlockIndex, expectedDepth))
                return false;
        }
        return true;
    }

    size_t StackDepth() const
    {
        return m_stack.size();
    }

  private:
    enum class Kind { Expr, LocalAddr, Address };
    struct RuntimeArrayMetadata
    {
        uint64_t headerAddress = 0;
        uint64_t dataAddress = 0;
        uint64_t baseIndex = 0;
        uint32_t stride = 0;
        bool global = false;
    };

    struct RuntimePointerProvenance
    {
        RuntimeArrayMetadata array;
        int64_t offsetBytes = 0;
        bool dynamicOffset = false;
    };

    struct Value
    {
        BinaryNinja::ExprId expr = 0;
        Kind kind = Kind::Expr;
        uint32_t index = 0;
        uint64_t address = 0;
        std::optional<uint32_t> constValue;
        std::optional<uint64_t> runtimeDataAddress;
        std::optional<RuntimeArrayMetadata> runtimeArray;
        std::optional<RuntimePointerProvenance> runtimePointer;
    };

    using BinaryOp = BinaryNinja::ExprId (BinaryNinja::LowLevelILFunction::*)(size_t, BinaryNinja::ExprId, BinaryNinja::ExprId, uint32_t, const BinaryNinja::ILSourceLocation&);
    using CompareOp = BinaryNinja::ExprId (BinaryNinja::LowLevelILFunction::*)(size_t, BinaryNinja::ExprId, BinaryNinja::ExprId, const BinaryNinja::ILSourceLocation&);
    using UnaryOp = BinaryNinja::ExprId (BinaryNinja::LowLevelILFunction::*)(size_t, BinaryNinja::ExprId, uint32_t, const BinaryNinja::ILSourceLocation&);

    void Push(BinaryNinja::ExprId expr, std::optional<uint32_t> constValue = std::nullopt) { m_stack.push_back(Value{expr, Kind::Expr, 0, 0, constValue, std::nullopt}); }
    void PushAddress(uint64_t address) { m_stack.push_back(Value{m_il.ConstPointer(4, address), Kind::Address, 0, address, std::nullopt, std::nullopt}); }
    void PushLocalAddr(uint32_t index) { m_stack.push_back(Value{m_il.Const(4, index), Kind::LocalAddr, index, 0, std::nullopt, std::nullopt}); }
    bool Pop(Value& value)
    {
        if (m_stack.empty())
        {
            value = Value{m_il.Register(4, StackTemp(m_currentBlockIndex, m_syntheticInputCount++)), Kind::Expr, 0, 0, std::nullopt, std::nullopt};
            return true;
        }
        value = m_stack.back();
        m_stack.pop_back();
        return true;
    }

    bool Dup(size_t& len)
    {
        if (m_stack.empty())
        {
            Value value;
            Pop(value);
            m_stack.push_back(value);
        }
        m_stack.push_back(m_stack.back()); len = 1; return true;
    }
    bool Drop(size_t& len)
    {
        if (!m_stack.empty())
        {
            Value v;
            Pop(v);
        }
        len = 1;
        return true;
    }
    bool Binary(size_t& len, BinaryOp op)
    {
        Value rhs, lhs;
        if (!Pop(rhs)) return false;
        if (!Pop(lhs))
        {
            uint32_t fallbackSlot = m_seededStackDepth > 0 ? static_cast<uint32_t>(m_seededStackDepth - 1) : 0;
            lhs = Value{m_il.Register(4, StackTemp(m_currentBlockIndex, fallbackSlot)), Kind::Expr, 0, 0, std::nullopt, std::nullopt};
        }
        Push((m_il.*op)(4, lhs.expr, rhs.expr, 0, {})); len = 1; return true;
    }
    bool PointerAdd(size_t& len)
    {
        Value rhs, lhs;
        if (!Pop(rhs)) return false;
        if (!Pop(lhs))
        {
            uint32_t fallbackSlot = m_seededStackDepth > 0 ? static_cast<uint32_t>(m_seededStackDepth - 1) : 0;
            lhs = Value{m_il.Register(4, StackTemp(m_currentBlockIndex, fallbackSlot)), Kind::Expr, 0, 0, std::nullopt, std::nullopt};
        }

        auto expr = m_il.Add(4, lhs.expr, rhs.expr);
        auto provenance = PointerArithmeticProvenance(lhs, rhs, true);
        PushPointerArithmeticResult(expr, provenance);
        len = 1;
        return true;
    }
    bool PointerSub(size_t& len)
    {
        Value rhs, lhs;
        if (!Pop(rhs)) return false;
        if (!Pop(lhs))
        {
            uint32_t fallbackSlot = m_seededStackDepth > 0 ? static_cast<uint32_t>(m_seededStackDepth - 1) : 0;
            lhs = Value{m_il.Register(4, StackTemp(m_currentBlockIndex, fallbackSlot)), Kind::Expr, 0, 0, std::nullopt, std::nullopt};
        }

        auto expr = m_il.Sub(4, lhs.expr, rhs.expr);
        auto provenance = (!rhs.runtimePointer) ? lhs.runtimePointer : std::nullopt;
        if (provenance)
            provenance->dynamicOffset = true;
        PushPointerArithmeticResult(expr, provenance);
        len = 1;
        return true;
    }
    bool ImmBinary(size_t& len, uint32_t imm, BinaryOp op, size_t insnLen = 2)
    {
        Value lhs; if (!Pop(lhs)) return false;
        Push((m_il.*op)(4, lhs.expr, m_il.Const(4, imm), 0, {})); len = insnLen; return true;
    }
    bool PointerImmAdd(size_t& len, int64_t imm, size_t insnLen)
    {
        Value lhs;
        if (!Pop(lhs)) return false;
        auto expr = AddOffset(lhs.expr, imm);
        auto provenance = lhs.runtimePointer;
        if (provenance)
            provenance->dynamicOffset = true;
        PushPointerArithmeticResult(expr, provenance);
        len = insnLen;
        return true;
    }
    bool Unary(size_t& len, UnaryOp op)
    {
        Value v; if (!Pop(v)) return false;
        Push((m_il.*op)(4, v.expr, 0, {})); len = 1; return true;
    }
    template <typename Fn> bool UnaryCustom(size_t& len, Fn fn)
    {
        Value v; if (!Pop(v)) return false;
        Push(fn(v.expr)); len = 1; return true;
    }
    bool IsBitSet(size_t& len)
    {
        Value bit, value;
        if (!Pop(bit) || !Pop(value)) return false;
        Push(m_il.TestBit(4, value.expr, bit.expr));
        len = 1;
        return true;
    }
    bool Compare(size_t& len, CompareOp op)
    {
        Value rhs, lhs; if (!Pop(rhs) || !Pop(lhs)) return false;
        Push((m_il.*op)(4, lhs.expr, rhs.expr, {})); len = 1; return true;
    }
    bool Fmod(size_t& len)
    {
        Value divisor, dividend;
        if (!Pop(divisor) || !Pop(dividend)) return false;
        auto quotient = m_il.FloatDiv(4, dividend.expr, divisor.expr);
        auto truncatedQuotient = m_il.FloatToInt(4, quotient);
        auto floatTruncatedQuotient = m_il.IntToFloat(4, truncatedQuotient);
        auto product = m_il.FloatMult(4, floatTruncatedQuotient, divisor.expr);
        Push(m_il.FloatSub(4, dividend.expr, product));
        len = 1;
        return true;
    }
    bool F2V(size_t& len)
    {
        Value value;
        if (!Pop(value)) return false;
        Push(value.expr);
        Push(value.expr);
        Push(value.expr);
        len = 1;
        return true;
    }
    bool VectorBinary(size_t& len, BinaryOp op)
    {
        Value z1, y1, x1, z2, y2, x2;
        if (!Pop(z1) || !Pop(y1) || !Pop(x1) || !Pop(z2) || !Pop(y2) || !Pop(x2)) return false;
        Push((m_il.*op)(4, x2.expr, x1.expr, 0, {}));
        Push((m_il.*op)(4, y2.expr, y1.expr, 0, {}));
        Push((m_il.*op)(4, z2.expr, z1.expr, 0, {}));
        len = 1;
        return true;
    }
    bool VectorUnary(size_t& len, UnaryOp op)
    {
        Value z, y, x;
        if (!Pop(z) || !Pop(y) || !Pop(x)) return false;
        Push((m_il.*op)(4, x.expr, 0, {}));
        Push((m_il.*op)(4, y.expr, 0, {}));
        Push((m_il.*op)(4, z.expr, 0, {}));
        len = 1;
        return true;
    }
    bool StoreLocal(uint32_t index, size_t& len, size_t insnLen)
    {
        Value v; if (!Pop(v)) return false;
        m_il.AddInstruction(m_il.SetRegister(4, LocalTemp(index), v.expr)); len = insnLen; return true;
    }
    bool StoreAddress(uint64_t address, size_t& len, size_t insnLen)
    {
        Value v; if (!Pop(v)) return false;
        DefineRuntimeDataAddress(address);
        m_il.AddInstruction(m_il.Store(4, m_il.ConstPointer(4, address), v.expr)); len = insnLen; return true;
    }
    bool Load(size_t& len)
    {
        Value ptr; if (!Pop(ptr)) return false;
        if (ptr.kind == Kind::LocalAddr)
            Push(m_il.Register(4, LocalTemp(ptr.index)));
        else if (ptr.kind == Kind::Address)
        {
            DefineRuntimeDataAddress(ptr.address);
            Push(m_il.Load(4, m_il.ConstPointer(4, ptr.address)));
        }
        else
        {
            Push(m_il.Load(4, ptr.expr));
        }
        len = 1; return true;
    }
    bool LoadN(size_t& len)
    {
        Value address, count;
        if (!Pop(address) || !Pop(count) || !count.constValue || *count.constValue > 64)
            return false;
        for (uint32_t i = 0; i < *count.constValue; i++)
        {
            if (address.kind == Kind::LocalAddr)
                Push(m_il.Register(4, LocalTemp(address.index + i)));
            else
            {
                Push(m_il.Load(4, AddOffset(address.expr, static_cast<int64_t>(i) * 4)));
            }
        }
        len = 1;
        return true;
    }
    bool Store(size_t& len, bool reverse)
    {
        Value ptr, val;
        if (reverse)
        {
            if (!Pop(val) || m_stack.empty())
                return false;
            ptr = m_stack.back();
        }
        else
        {
            if (!Pop(ptr) || !Pop(val))
                return false;
        }
        if (ptr.kind == Kind::LocalAddr)
            m_il.AddInstruction(m_il.SetRegister(4, LocalTemp(ptr.index), val.expr));
        else if (ptr.kind == Kind::Address)
        {
            DefineRuntimeDataAddress(ptr.address);
            m_il.AddInstruction(m_il.Store(4, m_il.ConstPointer(4, ptr.address), val.expr));
        }
        else
        {
            m_il.AddInstruction(m_il.Store(4, ptr.expr, val.expr));
        }
        len = 1; return true;
    }
    bool StoreN(size_t& len)
    {
        Value address, count;
        if (!Pop(address) || !Pop(count) || !count.constValue || *count.constValue > 64)
            return false;
        for (uint32_t i = 0; i < *count.constValue; i++)
        {
            Value value;
            if (!Pop(value)) return false;
            uint32_t reverseIndex = *count.constValue - i - 1;
            if (address.kind == Kind::LocalAddr)
                m_il.AddInstruction(m_il.SetRegister(4, LocalTemp(address.index + reverseIndex), value.expr));
            else
            {
                m_il.AddInstruction(m_il.Store(4, AddOffset(address.expr, static_cast<int64_t>(reverseIndex) * 4), value.expr));
            }
        }
        len = 1;
        return true;
    }
    BinaryNinja::ExprId ArrayElementAddress(const Value& address, const Value& index, uint32_t stride)
    {
        if (address.kind == Kind::Address)
        {
            uint64_t dataAddress = address.address + 4;
            auto metadata = GetRuntimeArrayMetadata(address.address, dataAddress, stride);
            if (metadata)
                DefineRuntimeArrayDataAddress(*metadata);
            return m_il.Add(4, RuntimePointer(dataAddress), m_il.Mult(4, m_il.Const(4, stride * 4), index.expr));
        }
        BinaryNinja::ExprId cellOffset = m_il.Add(4, m_il.Const(4, 1), m_il.Mult(4, m_il.Const(4, stride), index.expr));
        return m_il.Add(4, address.expr, m_il.Mult(4, cellOffset, m_il.Const(4, 4)));
    }
    Value ArrayElementValue(const Value& address, const Value& index, uint32_t stride)
    {
        if (address.kind == Kind::Address)
        {
            uint64_t dataAddress = address.address + 4;
            auto metadata = GetRuntimeArrayMetadata(address.address, dataAddress, stride);
            if (metadata)
                DefineRuntimeArrayDataAddress(*metadata);
            auto provenance = metadata ? std::optional<RuntimePointerProvenance>{RuntimePointerProvenance{*metadata, 0, true}} : std::nullopt;
            return Value{m_il.Add(4, RuntimePointer(dataAddress), m_il.Mult(4, m_il.Const(4, stride * 4), index.expr)), Kind::Expr, 0, 0, std::nullopt, dataAddress, metadata, provenance};
        }
        auto provenance = address.runtimePointer;
        if (provenance)
            provenance->dynamicOffset = true;
        return Value{ArrayElementAddress(address, index, stride), Kind::Expr, 0, 0, std::nullopt, address.runtimeDataAddress, address.runtimeArray, provenance};
    }
    bool Array(uint32_t stride, size_t& len, size_t insnLen)
    {
        Value address, index;
        if (!Pop(address) || !Pop(index)) return false;
        m_stack.push_back(ArrayElementValue(address, index, stride));
        len = insnLen;
        return true;
    }
    bool ArrayLoad(uint32_t stride, size_t& len, size_t insnLen)
    {
        Value address, index;
        if (!Pop(address) || !Pop(index)) return false;
        auto elem = ArrayElementValue(address, index, stride);
        Push(m_il.Load(4, elem.expr));
        len = insnLen;
        return true;
    }
    bool ArrayStore(uint32_t stride, size_t& len, size_t insnLen)
    {
        Value address, index, value;
        if (!Pop(address) || !Pop(index) || !Pop(value)) return false;
        auto elem = ArrayElementValue(address, index, stride);
        m_il.AddInstruction(m_il.Store(4, elem.expr, value.expr));
        len = insnLen;
        return true;
    }
    BinaryNinja::ExprId AddOffset(BinaryNinja::ExprId base, int64_t offsetBytes)
    {
        if (offsetBytes < 0)
            return m_il.Sub(4, base, m_il.Const(4, static_cast<uint64_t>(-offsetBytes)));
        return m_il.Add(4, base, m_il.Const(4, static_cast<uint64_t>(offsetBytes)));
    }
    BinaryNinja::ExprId RuntimePointer(uint64_t address)
    {
        return m_il.ConstPointer(4, address);
    }
    std::optional<RuntimePointerProvenance> PointerArithmeticProvenance(const Value& lhs, const Value& rhs, bool commutative)
    {
        if (lhs.runtimePointer && !rhs.runtimePointer)
        {
            auto provenance = lhs.runtimePointer;
            provenance->dynamicOffset = true;
            return provenance;
        }
        if (commutative && rhs.runtimePointer && !lhs.runtimePointer)
        {
            auto provenance = rhs.runtimePointer;
            provenance->dynamicOffset = true;
            return provenance;
        }
        return std::nullopt;
    }
    std::optional<RuntimePointerProvenance> RuntimePointerWithOffset(
        const std::optional<RuntimePointerProvenance>& provenance, int64_t offsetBytes)
    {
        if (!provenance)
            return std::nullopt;
        auto result = provenance;
        result->offsetBytes += offsetBytes;
        return result;
    }
    void PushPointerArithmeticResult(
        BinaryNinja::ExprId expr, const std::optional<RuntimePointerProvenance>& provenance)
    {
        if (!provenance)
        {
            Push(expr);
            return;
        }
        DefineRuntimePointerAlias(*provenance);
        m_stack.push_back(Value{
            expr, Kind::Expr, 0, 0, std::nullopt, provenance->array.dataAddress, provenance->array, provenance});
    }
    bool IOffset(size_t& len)
    {
        Value base, index;
        if (!Pop(base) || !Pop(index)) return false;
        auto provenance = base.runtimePointer;
        if (provenance)
            provenance->dynamicOffset = true;
        m_stack.push_back(Value{m_il.Add(4, base.expr, m_il.Mult(4, index.expr, m_il.Const(4, 4))), Kind::Expr, 0, 0, std::nullopt, base.runtimeDataAddress, base.runtimeArray, provenance});
        len = 1;
        return true;
    }
    bool IOffsetImm(size_t& len, int64_t operand, size_t insnLen)
    {
        Value base;
        if (!Pop(base)) return false;
        int64_t offsetBytes = operand * 4;
        if (base.kind == Kind::Address)
        {
            DefineRuntimeAddressOffsetAlias(base.address, offsetBytes);
            PushAddress(static_cast<uint64_t>(static_cast<int64_t>(base.address) + offsetBytes));
        }
        else
        {
            auto provenance = RuntimePointerWithOffset(base.runtimePointer, offsetBytes);
            if (provenance)
                DefineRuntimePointerAlias(*provenance);
            else if (base.runtimeArray)
                DefineRuntimeArrayOffsetAlias(*base.runtimeArray, offsetBytes);
            auto expr = AddOffset(base.expr, offsetBytes);
            m_stack.push_back(Value{expr, Kind::Expr, 0, 0, std::nullopt, base.runtimeDataAddress, base.runtimeArray, provenance});
        }
        len = insnLen;
        return true;
    }
    bool IOffsetLoadImm(size_t& len, int64_t operand, size_t insnLen)
    {
        Value base;
        if (!Pop(base)) return false;
        int64_t offsetBytes = operand * 4;
        if (base.kind == Kind::Address)
        {
            uint64_t address = static_cast<uint64_t>(static_cast<int64_t>(base.address) + offsetBytes);
            DefineRuntimeAddressOffsetAlias(base.address, offsetBytes);
            DefineRuntimeDataAddress(address);
            Push(m_il.Load(4, m_il.ConstPointer(4, address)));
        }
        else
        {
            auto provenance = RuntimePointerWithOffset(base.runtimePointer, offsetBytes);
            if (provenance)
                DefineRuntimePointerAlias(*provenance);
            else if (base.runtimeArray)
                DefineRuntimeArrayOffsetAlias(*base.runtimeArray, offsetBytes);
            Push(m_il.Load(4, AddOffset(base.expr, offsetBytes)));
        }
        len = insnLen;
        return true;
    }
    bool IOffsetStoreImm(size_t& len, int64_t operand, size_t insnLen)
    {
        Value base, value;
        if (!Pop(base) || !Pop(value)) return false;
        int64_t offsetBytes = operand * 4;
        if (base.kind == Kind::Address)
        {
            uint64_t address = static_cast<uint64_t>(static_cast<int64_t>(base.address) + offsetBytes);
            DefineRuntimeAddressOffsetAlias(base.address, offsetBytes);
            DefineRuntimeDataAddress(address);
            m_il.AddInstruction(m_il.Store(4, m_il.ConstPointer(4, address), value.expr));
        }
        else
        {
            auto provenance = RuntimePointerWithOffset(base.runtimePointer, offsetBytes);
            if (provenance)
                DefineRuntimePointerAlias(*provenance);
            else if (base.runtimeArray)
                DefineRuntimeArrayOffsetAlias(*base.runtimeArray, offsetBytes);
            m_il.AddInstruction(m_il.Store(4, AddOffset(base.expr, offsetBytes), value.expr));
        }
        len = insnLen;
        return true;
    }
    bool PopDiscard(size_t& len, size_t count, size_t insnLen)
    {
        for (size_t i = 0; i < count; i++)
        {
            Value v;
            if (!Pop(v)) return false;
        }
        m_il.AddInstruction(m_il.Nop());
        len = insnLen;
        return true;
    }
    bool EmitTextLabelIntrinsic(uint32_t intrinsic, std::vector<BinaryNinja::ExprId> params, size_t& len, size_t insnLen)
    {
        m_il.AddInstruction(m_il.Intrinsic({}, intrinsic, params));
        len = insnLen;
        return true;
    }
    bool TextLabelAssignString(uint8_t size, size_t& len)
    {
        Value dst, src;
        if (!Pop(dst) || !Pop(src)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelAssignString, {dst.expr, src.expr, m_il.Const(4, size)}, len, 2);
    }
    bool TextLabelAssignInt(uint8_t size, size_t& len)
    {
        Value dst, value;
        if (!Pop(dst) || !Pop(value)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelAssignInt, {dst.expr, value.expr, m_il.Const(4, size)}, len, 2);
    }
    bool TextLabelAppendString(uint8_t size, size_t& len)
    {
        Value dst, src;
        if (!Pop(dst) || !Pop(src)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelAppendString, {dst.expr, src.expr, m_il.Const(4, size)}, len, 2);
    }
    bool TextLabelAppendInt(uint8_t size, size_t& len)
    {
        Value dst, value;
        if (!Pop(dst) || !Pop(value)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelAppendInt, {dst.expr, value.expr, m_il.Const(4, size)}, len, 2);
    }
    bool TextLabelCopy(size_t& len)
    {
        Value dst, repeat, src;
        if (!Pop(dst) || !Pop(repeat) || !Pop(src)) return false;
        return EmitTextLabelIntrinsic(Intrin_TextLabelCopy, {dst.expr, repeat.expr, src.expr}, len, 1);
    }
    bool String(size_t& len)
    {
        Value off; if (!Pop(off)) return false;
        auto section = m_view ? m_view->GetSectionByName("STRINGS") : nullptr;
        if (!section) return false;
        Push(m_il.Add(4, m_il.ConstPointer(4, section->GetStart()), off.expr)); len = 1; return true;
    }
    bool StringHash(size_t& len)
    {
        Value str; if (!Pop(str)) return false;
        uint32_t resultTemp = CallResultTemp(m_il.GetCurrentAddress());
        m_il.AddInstruction(m_il.Intrinsic({BinaryNinja::RegisterOrFlag::Register(resultTemp)}, Intrin_StringHash, {str.expr}));
        Push(m_il.Register(4, resultTemp));
        len = 1;
        return true;
    }
    bool Jump(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (len < 3) return false;
        if (!IsValidBranchTarget(BranchTarget(addr, opcode))) return false;
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;
        EmitGoto(BranchTarget(addr, opcode)); len = 3; return true;
    }
    bool Jz(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        Value cond; if (len < 3 || !Pop(cond)) return false;
        uint64_t branchTarget = BranchTarget(addr, opcode);
        uint64_t fallthroughTarget = addr + 3;
        if (!IsValidBranchTarget(branchTarget) || !IsValidBranchTarget(fallthroughTarget)) return false;
        if (branchTarget == fallthroughTarget)
        {
            if (m_expectedOutgoingStackDepth)
            {
                if (m_expectedFallthroughBlockIndex)
                {
                    if (!StoreStackOutputs(*m_expectedFallthroughBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (m_expectedBranchBlockIndex)
                {
                    if (!StoreStackOutputs(*m_expectedBranchBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (!StoreStackOutputs(*m_expectedOutgoingStackDepth))
                    return false;
            }
            len = 3;
            return true;
        }
        if (cond.constValue)
        {
            bool takeBranch = *cond.constValue == 0;
            uint64_t liveTarget = takeBranch ? branchTarget : fallthroughTarget;
            if (m_expectedOutgoingStackDepth)
            {
                auto liveBlockIndex = takeBranch ? m_expectedBranchBlockIndex : m_expectedFallthroughBlockIndex;
                if (liveBlockIndex)
                {
                    if (!StoreStackOutputs(*liveBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (!StoreStackOutputs(*m_expectedOutgoingStackDepth))
                    return false;
            }
            EmitGoto(liveTarget);
            len = 3;
            return true;
        }
        if (m_expectedOutgoingStackDepth && m_expectedBranchBlockIndex && m_expectedFallthroughBlockIndex)
        {
            EmitJzWithEdgeStackStores(cond.expr, branchTarget, fallthroughTarget, *m_expectedBranchBlockIndex,
                                      *m_expectedFallthroughBlockIndex, *m_expectedOutgoingStackDepth, m_explicitFallthroughGoto);
            len = 3;
            return true;
        }
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;
        EmitJz(cond.expr, branchTarget); len = 3; return true;
    }
    bool CompareJz(const uint8_t* opcode, uint64_t addr, size_t& len, CompareOp op)
    {
        Value rhs, lhs; if (len < 3 || !Pop(rhs) || !Pop(lhs)) return false;
        auto cond = (m_il.*op)(4, lhs.expr, rhs.expr, {});
        uint64_t branchTarget = BranchTarget(addr, opcode);
        uint64_t fallthroughTarget = addr + 3;
        if (!IsValidBranchTarget(branchTarget) || !IsValidBranchTarget(fallthroughTarget)) return false;
        if (branchTarget == fallthroughTarget)
        {
            if (m_expectedOutgoingStackDepth)
            {
                if (m_expectedFallthroughBlockIndex)
                {
                    if (!StoreStackOutputs(*m_expectedFallthroughBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (m_expectedBranchBlockIndex)
                {
                    if (!StoreStackOutputs(*m_expectedBranchBlockIndex, *m_expectedOutgoingStackDepth)) return false;
                }
                else if (!StoreStackOutputs(*m_expectedOutgoingStackDepth))
                    return false;
            }
            len = 3;
            return true;
        }
        if (m_expectedOutgoingStackDepth && m_expectedBranchBlockIndex && m_expectedFallthroughBlockIndex)
        {
            EmitJzWithEdgeStackStores(cond, branchTarget, fallthroughTarget, *m_expectedBranchBlockIndex,
                                      *m_expectedFallthroughBlockIndex, *m_expectedOutgoingStackDepth, m_explicitFallthroughGoto);
            len = 3;
            return true;
        }
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;
        EmitJz(cond, branchTarget); len = 3; return true;
    }
    bool Switch(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (len < 2) return false;
        Value selector; if (!Pop(selector)) return false;
        if (m_expectedOutgoingStackDepth && !StoreStackOutputs(*m_expectedOutgoingStackDepth)) return false;

        auto decodedSwitch = DecodeYSCSwitchInfo(m_view, addr, opcode, len);
        if (!decodedSwitch)
            return false;

        for (const auto& switchCase : decodedSwitch->m_cases)
            if (!IsValidBranchTarget(switchCase.m_target))
                return false;
        if (!IsValidBranchTarget(decodedSwitch->m_tableEnd))
            return false;

        std::vector<BinaryNinja::ArchAndAddr> indirectBranches;
        indirectBranches.reserve(decodedSwitch->m_cases.size() + 1);
        for (const auto& switchCase : decodedSwitch->m_cases)
            indirectBranches.emplace_back(m_arch, switchCase.m_target);
        indirectBranches.emplace_back(m_arch, decodedSwitch->m_tableEnd);
        m_il.SetIndirectBranches(indirectBranches);

        std::vector<BinaryNinja::LowLevelILLabel> falseLabels(decodedSwitch->m_cases.size());
        for (size_t i = 0; i < decodedSwitch->m_cases.size(); i++)
        {
            const auto& switchCase = decodedSwitch->m_cases[i];
            BinaryNinja::LowLevelILLabel trueLabel;
            m_il.AddInstruction(m_il.If(m_il.CompareEqual(4, selector.expr, m_il.Const(4, switchCase.m_case)), trueLabel, falseLabels[i]));
            m_il.MarkLabel(trueLabel);
            EmitGoto(switchCase.m_target);
            m_il.MarkLabel(falseLabels[i]);
        }
        EmitGoto(decodedSwitch->m_tableEnd);
        len = decodedSwitch->m_tableEnd - addr;
        return true;
    }
    bool Native(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (len < 4) return false;
        uint8_t retCount = opcode[1] & 3;
        uint8_t paramCount = (opcode[1] >> 2) & 0x3f;
        if (paramCount > 16)
            return false;
        std::vector<Value> args;
        for (uint8_t i = 0; i < paramCount; i++) { Value v; if (!Pop(v)) return false; args.push_back(v); }
        uint32_t argIndex = 0;
        for (auto it = args.rbegin(); it != args.rend(); ++it, ++argIndex)
            m_il.AddInstruction(m_il.SetRegister(4, ArgReg(argIndex), it->expr));
        auto nativeSection = m_view ? m_view->GetSectionByName("NATIVES") : nullptr;
        if (!nativeSection) return false;
        uint64_t nativeAddress = nativeSection->GetStart() + static_cast<uint64_t>((opcode[2] << 8) | opcode[3]) * 8;
        m_il.AddInstruction(m_il.Call(m_il.ExternPointer(8, nativeAddress, 0)));
        if (m_view)
        {
            if (auto symbol = m_view->GetSymbolByAddress(nativeAddress))
            {
                if (symbol->GetRawName() == "native_SCRIPT_TERMINATE_THIS_THREAD")
                    m_il.AddInstruction(m_il.NoReturn());
            }
        }
        for (uint8_t i = 0; i < retCount; i++)
        {
            uint32_t resultTemp = CallResultTemp(addr + i);
            m_il.AddInstruction(m_il.SetRegister(4, resultTemp, m_il.Register(4, ReturnReg(i))));
            Push(m_il.Register(4, resultTemp));
        }
        len = 4;
        return true;
    }
    bool Call(const uint8_t* opcode, uint64_t addr, size_t& len)
    {
        if (len < 4) return false;
        auto code = m_view ? m_view->GetSectionByName("CODE") : nullptr;
        if (!code) return false;
        uint64_t target = code->GetStart() + DecodeU24(opcode + 1);
        if (!IsYSCValidCallTarget(m_view, target, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH)) return false;
        uint8_t paramCount = GetEnterParamCount(m_view, target);
        uint8_t retCount = FindFirstLeaveReturnCount(m_view, target);
        if (paramCount > 16)
            return false;
        std::vector<Value> args;
        for (uint8_t i = 0; i < paramCount; i++) { Value v; if (!Pop(v)) return false; args.push_back(v); }
        uint32_t argIndex = 0;
        for (auto it = args.rbegin(); it != args.rend(); ++it, ++argIndex)
            m_il.AddInstruction(m_il.SetRegister(4, ArgReg(argIndex), it->expr));
        m_il.AddInstruction(m_il.Call(m_il.ConstPointer(4, target)));
        for (uint8_t i = 0; i < retCount; i++)
        {
            uint32_t resultTemp = CallResultTemp(addr + i);
            m_il.AddInstruction(m_il.SetRegister(4, resultTemp, m_il.Register(4, ReturnReg(i))));
            Push(m_il.Register(4, resultTemp));
        }
        len = 4;
        return true;
    }
    bool CallIndirect(size_t& len)
    {
        Value target;
        if (!Pop(target)) return false;
        m_il.AddInstruction(m_il.Call(target.expr));
        len = 1;
        return true;
    }
    bool Leave(const uint8_t* opcode, size_t& len)
    {
        if (len < 3) return false;
        uint8_t retCount = opcode[2];
        if (retCount > 0)
        {
            Value ret; if (!Pop(ret)) return false;
            m_il.AddInstruction(m_il.SetRegister(4, Reg_R1, ret.expr));
        }
        m_il.AddInstruction(m_il.Return(m_il.ConstPointer(4, 0)));
        len = 3;
        return true;
    }

    bool Enter(const uint8_t* opcode, size_t& len)
    {
        if (len < 5) return false;
        uint8_t paramCount = opcode[1];
        uint8_t nameLen = opcode[4];
        if (len < static_cast<size_t>(5 + nameLen)) return false;
        if (paramCount > 16) return false;
        m_stack.clear();
        for (uint32_t i = 0; i < paramCount; i++)
            m_il.AddInstruction(m_il.SetRegister(4, LocalTemp(i), m_il.Register(4, ArgReg(i))));
        m_il.AddInstruction(m_il.Nop());
        len = 5 + nameLen;
        return true;
    }

    void EmitGoto(uint64_t target)
    {
        if (auto label = m_il.GetLabelForAddress(m_arch, target))
            m_il.AddInstruction(m_il.Goto(*label));
        else
            m_il.AddInstruction(m_il.Jump(m_il.ConstPointer(4, target)));
    }
    bool IsValidBranchTarget(uint64_t target)
    {
        if (!m_validInstructionStarts.empty())
            return m_validInstructionStarts.contains(target);
        return IsYSCIndexedInstructionStart(m_view, target, YSC_MAX_INTERNAL_INSTRUCTION_LENGTH);
    }
    void EmitJz(BinaryNinja::ExprId cond, uint64_t falseTarget, std::optional<uint64_t> trueTarget = std::nullopt)
    {
        BinaryNinja::LowLevelILLabel trueLabel;
        BinaryNinja::LowLevelILLabel falseLabel;
        m_il.AddInstruction(m_il.If(cond, trueLabel, falseLabel));
        m_il.MarkLabel(falseLabel);
        EmitGoto(falseTarget);
        m_il.MarkLabel(trueLabel);
        if (trueTarget)
            EmitGoto(*trueTarget);
    }

    void EmitJzWithEdgeStackStores(BinaryNinja::ExprId cond, uint64_t falseTarget, uint64_t trueTarget,
                                   uint32_t falseTargetBlockIndex, uint32_t trueTargetBlockIndex, size_t expectedDepth,
                                   bool explicitTrueTarget)
    {
        // For YSC short-circuit expressions, the same VM-stack value is available on both
        // successors immediately after the conditional pop. Emitting the successor-input
        // stores before the branch gives BN a simple "temp = cond; if (!temp) temp |= rhs"
        // shape instead of edge-local assignments that HLIL tends to linearize as
        // confusing overwrites.
        StoreStackOutputs(falseTargetBlockIndex, expectedDepth);
        StoreStackOutputs(trueTargetBlockIndex, expectedDepth);
        EmitJz(cond, falseTarget, explicitTrueTarget ? std::optional<uint64_t>(trueTarget) : std::nullopt);
    }
    uint64_t StaticAddress(uint32_t operand)
    {
        auto section = m_view ? m_view->GetSectionByName("STATICS") : nullptr;
        uint64_t address = (section ? section->GetStart() : 0) + operand * 4;
        if (section && m_view && address >= section->GetStart() && address < section->GetEnd())
            DefineAutoInt32DataSymbol(m_view, address, fmt::format("Local_{}", operand));
        return address;
    }
    uint64_t GlobalAddress(uint32_t operand)
    {
        auto section = m_view ? m_view->GetSectionByName("GLOBALS") : nullptr;
        uint32_t block = operand >> 18;
        uint32_t needle = operand & 0x3ffff;
        uint64_t address = (section ? section->GetStart() : 0) + (static_cast<uint64_t>(block) * (1 << 18) + needle) * 4;
        if (m_view)
            DefineAutoInt32DataSymbol(m_view, address, fmt::format("Global_{}", operand));
        return address;
    }

    void DefineRuntimeDataAddress(uint64_t address)
    {
        if (!YSCRuntimeArrayMetadataEnabled() || !m_view)
            return;
        auto globals = m_view->GetSectionByName("GLOBALS");
        if (globals && address >= globals->GetStart() && address < globals->GetEnd())
        {
            uint64_t index = (address - globals->GetStart()) / 4;
            DefineAutoInt32DataSymbol(m_view, address, fmt::format("Global_{}", index));
        }
    }

    inline static std::mutex g_runtimeArrayShapeMutex;
    inline static std::set<std::tuple<uintptr_t, uint64_t, uint32_t>> g_definedRuntimeArrayBases;
    inline static std::set<std::tuple<uintptr_t, uint64_t, uint32_t>> g_definedRuntimeArrayOffsetAliases;
    inline static std::map<std::tuple<uintptr_t, uint64_t, uint32_t>, size_t> g_runtimeArrayOffsetAliasCounts;

    uintptr_t RuntimeMetadataViewKey() const
    {
        return reinterpret_cast<uintptr_t>(m_view);
    }

    std::string RuntimeMetadataPrefix(const RuntimeArrayMetadata& metadata) const
    {
        return metadata.global ? "Global" : "Static";
    }

    std::optional<RuntimeArrayMetadata> GetRuntimeArrayMetadata(uint64_t headerAddress, uint64_t dataAddress, uint32_t stride) const
    {
        if (!m_view || stride == 0 || stride > 4096)
            return std::nullopt;
        auto globals = m_view->GetSectionByName("GLOBALS");
        auto statics = m_view->GetSectionByName("STATICS");
        if (globals && dataAddress >= globals->GetStart() && dataAddress < globals->GetEnd())
            return RuntimeArrayMetadata{headerAddress, dataAddress, (headerAddress - globals->GetStart()) / 4, stride, true};
        if (statics && dataAddress >= statics->GetStart() && dataAddress < statics->GetEnd())
            return RuntimeArrayMetadata{headerAddress, dataAddress, (headerAddress - statics->GetStart()) / 4, stride, false};
        return std::nullopt;
    }

    void DefineRuntimeArrayDataAddress(const RuntimeArrayMetadata& metadata)
    {
        if (!YSCRuntimeArrayMetadataEnabled() || !m_view)
            return;
        std::lock_guard<std::mutex> guard(g_runtimeArrayShapeMutex);
        DefineRuntimeArrayDataAddressLocked(metadata);
    }

    void DefineRuntimeArrayDataAddressLocked(const RuntimeArrayMetadata& metadata)
    {
        if (!m_view || metadata.stride == 0 || metadata.stride > 4096)
            return;

        auto key = std::make_tuple(RuntimeMetadataViewKey(), metadata.dataAddress, metadata.stride);
        if (!g_definedRuntimeArrayBases.insert(key).second)
            return;

        DefineAutoInt32DataSymbol(m_view, metadata.dataAddress,
            fmt::format("{}_{}_arr{}_data", RuntimeMetadataPrefix(metadata), metadata.baseIndex, metadata.stride));
    }

    uint64_t RuntimeArrayMaxOffsetBytes(const RuntimeArrayMetadata& metadata) const
    {
        uint64_t strideBytes = static_cast<uint64_t>(metadata.stride) * 4;
        if (strideBytes < 0x100)
            strideBytes = 0x100;
        if (strideBytes > 0x1000)
            strideBytes = 0x1000;
        return strideBytes;
    }

    void DefineRuntimePointerAlias(const RuntimePointerProvenance& provenance)
    {
        if (provenance.offsetBytes <= 0)
            return;
        DefineRuntimeArrayOffsetAlias(provenance.array, provenance.offsetBytes);
    }

    void DefineRuntimeArrayOffsetAlias(const RuntimeArrayMetadata& metadata, int64_t offsetBytes)
    {
        if (!YSCRuntimeArrayMetadataEnabled() || !m_view || offsetBytes < 0 ||
            static_cast<uint64_t>(offsetBytes) > RuntimeArrayMaxOffsetBytes(metadata))
            return;
        uint64_t aliasAddress = metadata.dataAddress + static_cast<uint64_t>(offsetBytes);
        if (!RuntimeAddressInMetadataSection(metadata, aliasAddress))
            return;

        std::lock_guard<std::mutex> guard(g_runtimeArrayShapeMutex);
        DefineRuntimeArrayDataAddressLocked(metadata);
        auto countKey = std::make_tuple(RuntimeMetadataViewKey(), metadata.dataAddress, metadata.stride);
        auto& aliasCount = g_runtimeArrayOffsetAliasCounts[countKey];
        if (aliasCount >= YSC_MAX_RUNTIME_ARRAY_OFFSET_ALIASES_PER_BASE)
            return;
        auto aliasKey = std::make_tuple(RuntimeMetadataViewKey(), aliasAddress, metadata.stride);
        if (!g_definedRuntimeArrayOffsetAliases.insert(aliasKey).second)
            return;
        aliasCount++;
        DefineAutoInt32DataSymbol(m_view, aliasAddress,
            fmt::format("{}_{}_arr{}_f{:x}", RuntimeMetadataPrefix(metadata), metadata.baseIndex,
                metadata.stride, static_cast<uint64_t>(offsetBytes)));
    }

    bool RuntimeAddressInMetadataSection(const RuntimeArrayMetadata& metadata, uint64_t address) const
    {
        auto section = metadata.global ? m_view->GetSectionByName("GLOBALS") : m_view->GetSectionByName("STATICS");
        return section && address >= section->GetStart() && address < section->GetEnd();
    }

    void DefineRuntimeAddressOffsetAlias(uint64_t baseAddress, int64_t offsetBytes)
    {
        if (!YSCRuntimeArrayMetadataEnabled() || !m_view || offsetBytes <= 0 || offsetBytes > 0x100)
            return;

        auto globals = m_view->GetSectionByName("GLOBALS");
        auto statics = m_view->GetSectionByName("STATICS");
        BinaryNinja::Section* section = nullptr;
        std::string prefix;
        if (globals && baseAddress >= globals->GetStart() && baseAddress < globals->GetEnd())
        {
            section = globals;
            prefix = "Global";
        }
        else if (statics && baseAddress >= statics->GetStart() && baseAddress < statics->GetEnd())
        {
            section = statics;
            prefix = "Static";
        }
        if (!section)
            return;

        uint64_t aliasAddress = baseAddress + static_cast<uint64_t>(offsetBytes);
        if (aliasAddress >= section->GetEnd())
            return;

        std::lock_guard<std::mutex> guard(g_runtimeArrayShapeMutex);
        auto countKey = std::make_tuple(RuntimeMetadataViewKey(), baseAddress, 0);
        auto& aliasCount = g_runtimeArrayOffsetAliasCounts[countKey];
        if (aliasCount >= YSC_MAX_RUNTIME_ARRAY_OFFSET_ALIASES_PER_BASE)
            return;
        auto aliasKey = std::make_tuple(RuntimeMetadataViewKey(), aliasAddress, 0);
        if (!g_definedRuntimeArrayOffsetAliases.insert(aliasKey).second)
            return;
        aliasCount++;
        uint64_t baseIndex = (baseAddress - section->GetStart()) / 4;
        DefineAutoInt32DataSymbol(m_view, aliasAddress,
            fmt::format("{}_{}_f{:x}", prefix, baseIndex, static_cast<uint64_t>(offsetBytes)));
    }

    YSCArchitecture* m_arch;
    BinaryNinja::LowLevelILFunction& m_il;
    BinaryNinja::BinaryView* m_view;
    std::vector<Value> m_stack;
    bool m_symbolic = true;
    uint32_t m_currentBlockIndex = 0;
    size_t m_seededStackDepth = 0;
    uint32_t m_syntheticInputCount = 0;
    std::unordered_set<uint64_t> m_validInstructionStarts;
    std::optional<size_t> m_expectedOutgoingStackDepth;
    std::vector<uint32_t> m_expectedOutgoingBlockIndices;
    std::optional<uint32_t> m_expectedFallthroughBlockIndex;
    std::optional<uint32_t> m_expectedBranchBlockIndex;
    bool m_explicitFallthroughGoto = false;
};

YSCSymbolicLifter::YSCSymbolicLifter(YSCArchitecture* arch, BinaryNinja::LowLevelILFunction& il,
                                     BinaryNinja::BinaryView* view) :
    m_impl(std::make_unique<Impl>(arch, il, view))
{}

YSCSymbolicLifter::~YSCSymbolicLifter() = default;

bool YSCSymbolicLifter::Lift(const uint8_t* opcode, uint64_t addr, size_t& len)
{
    return m_impl->Lift(opcode, addr, len);
}

void YSCSymbolicLifter::DisableAndFlush()
{
    m_impl->DisableAndFlush();
}

void YSCSymbolicLifter::Reset()
{
    m_impl->Reset();
}

void YSCSymbolicLifter::SetExpectedOutgoingStackDepth(std::optional<size_t> depth,
                                                      std::vector<uint32_t> targetBlockIndices)
{
    m_impl->SetExpectedOutgoingStackDepth(depth, std::move(targetBlockIndices));
}

void YSCSymbolicLifter::SetExpectedBranchStackTargets(std::optional<size_t> depth,
                                                      std::optional<uint32_t> fallthroughBlockIndex,
                                                      std::optional<uint32_t> branchBlockIndex,
                                                      bool explicitFallthroughGoto)
{
    m_impl->SetExpectedBranchStackTargets(depth, fallthroughBlockIndex, branchBlockIndex, explicitFallthroughGoto);
}

void YSCSymbolicLifter::SeedStack(uint32_t blockIndex, size_t depth)
{
    m_impl->SeedStack(blockIndex, depth);
}

void YSCSymbolicLifter::SetValidInstructionStarts(std::unordered_set<uint64_t> instructionStarts)
{
    m_impl->SetValidInstructionStarts(std::move(instructionStarts));
}

bool YSCSymbolicLifter::StoreStackOutputs(uint32_t targetBlockIndex, size_t expectedDepth)
{
    return m_impl->StoreStackOutputs(targetBlockIndex, expectedDepth);
}

bool YSCSymbolicLifter::StoreStackOutputs(size_t expectedDepth)
{
    return m_impl->StoreStackOutputs(expectedDepth);
}

size_t YSCSymbolicLifter::StackDepth() const
{
    return m_impl->StackDepth();
}

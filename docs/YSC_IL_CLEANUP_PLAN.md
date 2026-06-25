# YSC IL / Pseudocode Cleanup Implementation Plan

This document is a handoff plan for improving the Binary Ninja YSC architecture plugin's IL generation and pseudocode quality without repeating prior regressions. It is intended for another agent/developer to pick up and implement.

## Context

Repository: `/home/tom/Documents/ysc-binja`

Primary test binary:

```text
/mnt/myshare/GTA Dumps/1.72/PC/Scripts/Raw/appmpbossagency.ysc
```

Important current files:

- `src/Architecture/YSCArchitecture.cpp`
  - `YSCSymbolicLifter` starts around line ~1121.
  - `YSCSymbolicLifter::Native` around line ~1782.
  - `YSCSymbolicLifter::Call` around line ~1815.
  - `YSCArchitecture::AnalyzeBasicBlocks` around line ~2177.
  - `YSCArchitecture::LiftFunction` around line ~2345.
- `src/Architecture/YSCArchitecture.hpp`
  - Architecture derives from `BinaryNinja::ArchitectureWithFunctionContext<YSCFunctionContext>`.
- `src/CallingConvention/CallingConvention.hpp`
  - Defines `YSCCallingConvention`, currently named `sccall`.
- `src/View/YSCView.cpp`
  - Defines `NATIVES`, `GLOBALS`, native symbols/types, statics, etc.

Recent bad experiment to avoid repeating:

- Do **not** call `Function::SetUserType` or `SetParameterVariables` from architecture analysis.
- Do **not** add a fake `LOCAL_FRAME` external section.
- Do **not** create field alias data symbols from `LOAD_N`/`STORE_N` automatically.
- Do **not** mutate large amounts of database state from lifter paths.

The last experiment caused noticeable lag/regressions and was reverted.

## Binary Ninja API findings

Researched against local BN C++ docs in `/home/tom/Tools/binaryninja/api-docs/cpp` and BN 5.2/5.3 changelogs.

### BN 5.3 relevant changes

BN 5.3 added architecture APIs for weird/VM architectures:

- Full function-level lifting via `Architecture::LiftFunction`.
- `FunctionLifterContext` provides:
  - `GetView()`
  - `GetPlatform()`
  - `GetBasicBlocks()`
  - `GetNoReturnCalls()`
  - `GetContextualReturns()`
  - `GetInlinedRemapping()`
  - `GetUserIndirectBranches()` / `GetAutoIndirectBranches()`
  - `GetInlinedCalls()`
  - `CheckForInlinedCall(...)`
  - `PrepareBlockTranslation(...)`
  - `PrepareToCopyForeignFunction(...)`
  - `GetForeignFunctionLiftedIL(...)`
  - `GetFunctionArchContext(...)`

Implication: YSC is exactly the kind of stack VM that should use `AnalyzeBasicBlocks` + `LiftFunction`. The plugin is already on this path and should continue using it.

### BN 5.2 relevant changes

BN 5.2/5.1-era APIs improved custom basic-block analysis and block label behavior. The current plugin's custom `AnalyzeBasicBlocks` is the right architecture-level place to fix CFG/instruction length bugs.

Other relevant 5.2 points:

- MLIL expression mappings in C++ were added.
- Type propagation and data structure handling improved, but should be leveraged through accurate IL and bounded type application, not symbol spam.
- Custom constants/strings exist but are not the primary solution for YSC call/global semantics.

### LLIL call API limitation

Local docs show:

```cpp
ExprId LowLevelILFunction::Call(ExprId dest, const ILSourceLocation& loc = {})
ExprId LowLevelILFunction::CallStackAdjust(ExprId dest, int64_t adjust, const std::map<uint32_t, int32_t>& regStackAdjust, const ILSourceLocation& loc = {})
```

There is no direct LLIL `Call(dest, params)` overload in this API. LLIL call arguments are normally recovered from calling convention + function type + assignments to argument registers.

Therefore, for real calls, the plugin should continue emitting argument-register assignments plus `Call(...)`, but make sure BN has reliable callee signatures and calling convention mapping.

### Intrinsic API

LLIL supports intrinsics:

```cpp
ExprId LowLevelILFunction::Intrinsic(
    const std::vector<RegisterOrFlag>& outputs,
    uint32_t intrinsic,
    const std::vector<ExprId>& params,
    uint32_t flags = 0,
    const ILSourceLocation& loc = {})
```

The plugin already has one intrinsic:

- `Intrin_StringHash`
- Name: `ysc_string_hash`

Intrinsics are viable for **VM-only concepts** that are not real machine calls or real memory pointers. They should not replace normal script calls unless absolutely necessary.

### Calling convention API

`CallingConvention` has overridable mapping helpers:

```cpp
Variable GetIncomingVariableForParameterVariable(const Variable& var, Function* func)
Variable GetParameterVariableForIncomingVariable(const Variable& var, Function* func)
```

The current `YSCCallingConvention` does not override these. It only provides argument register lists and return register info.

This is likely important for making BN correctly treat `ARG0`..`ARG15` as function parameters and calls as using those registers.

## Current observed pain points

### 1. High-arity script calls render poorly

Example target:

```text
sub_10001c78 ENTER 0xd, 0xf
```

Callers, especially around `sub_10001c88`, may set up many arguments but pseudocode can still degrade into detached expressions or `sub_10001c78()` without visible args.

Current symbolic call lowering:

```cpp
for args:
    SetRegister(ARGi, expr)
Call(ConstPointer(target))
for returns:
    temp = ReturnReg(i)
```

Because LLIL has no explicit call-param list, this depends on function type + calling convention mapping.

### 2. Local-address/native pointer semantics

YSC `LOCAL_U8` / `LOCAL_U16` pushes an address-like VM local reference. Passing this to natives can currently decompile as `nullptr` or an integer-like local index, which is semantically wrong.

Example bad output:

```c
native_NETWORK_NETWORK_IS_HANDLE_VALID(nullptr, 0xd)
```

### 3. Global/runtime array expressions are ugly

Desired output for patterns like:

```c
*(uint32_t*)((1 + 0xd * i) * 4 + 0x60797a78)
```

is approximately:

```c
Global_XXXX_data[0xd * i].field_10
```

or at least:

```c
*(&Global_XXXX_data + 0x34 * i + 0x10)
```

Previous attempts to create lots of field aliases from `LOAD_N`/`STORE_N` caused lag and should not be repeated automatically.

## Decision summary

Chosen approach, in order:

1. **Fix call argument recovery through calling convention + idempotent auto function signatures.**
2. **Introduce a small `ysc_local_ref(index)` intrinsic only for escaping local-address values.**
3. **Keep global/runtime array cleanup arithmetic-only and metadata-only; do not auto-create per-field symbols/types.**
4. **Add regression snapshots before and after each change.**

Rejected approaches:

- `SetUserType` / `SetParameterVariables` from architecture analysis: rejected as too invasive and already regressed.
- Fake `LOCAL_FRAME` memory section: rejected as janky and regressed.
- Per-field symbol creation during lifting: rejected as symbol spam / performance hazard.
- Replacing script calls with `ysc_call_*` intrinsics: rejected for normal output because user disliked it and it weakens call graph semantics.

## Phase 1: Correct script call argument rendering

### Goal

Make script calls render as real calls with arguments:

```c
sub_10001c78(a0, a1, ..., a12)
```

not:

```c
ARG0 = ...;
ARG1 = ...;
sub_10001c78();
```

### Implementation plan

#### 1.1 Make `ApplyYSCFunctionType` idempotent and safe

Current function is around `YSCArchitecture.cpp:238`.

It currently does:

```cpp
function->SetAutoType(Type::FunctionType(...));
function->SetAutoParameterVariables(...);
```

Keep auto-only behavior. Do **not** add user setters.

Change it to be idempotent:

- Compare desired param count with current auto/user function type if accessible.
- If already matching, return early.
- Avoid repeated `CreateAutoVariable` churn if variable already exists with same name/source.
- Use `Confidence` consistently but not user-level confidence.

Pseudo:

```cpp
static void ApplyYSCFunctionType(Function* function, BinaryView* view,
                                 const std::optional<YSCEnterInfo>& enter,
                                 const std::optional<uint8_t>& retCount)
{
    if (!function || !view || !enter || enter->m_paramCount > 16)
        return;

    if (ExistingAutoSignatureMatches(function, enter->m_paramCount, desiredReturnCount))
        return;

    Build params using RegisterVariableSourceType + ArgReg(i).
    SetAutoType(...);
    SetAutoParameterVariables(...);
}
```

Add debug logging only behind a low-noise condition or temporary setting, e.g. log only when changed.

#### 1.2 Override calling convention variable mapping

In `src/CallingConvention/CallingConvention.hpp`, implement:

```cpp
Variable GetIncomingVariableForParameterVariable(const Variable& var, Function* func) override;
Variable GetParameterVariableForIncomingVariable(const Variable& var, Function* func) override;
```

The idea: explicitly map parameter variables and incoming register variables for `ARG0`..`ARG15`.

For YSC, parameter variable and incoming variable are both register variables over `Reg_ARGn`. A conservative mapping can return `var` for register variables in the ARG range.

Pseudo:

```cpp
static bool IsArgRegister(uint32_t reg)
{
    return reg >= Reg_ARG0 && reg <= Reg_ARG15;
}

Variable GetIncomingVariableForParameterVariable(const Variable& var, Function*) override
{
    if (var.type == RegisterVariableSourceType && IsArgRegister(var.storage))
        return var;
    return CallingConvention::GetIncomingVariableForParameterVariable(var, func);
}

Variable GetParameterVariableForIncomingVariable(const Variable& var, Function*) override
{
    if (var.type == RegisterVariableSourceType && IsArgRegister(var.storage))
        return var;
    return CallingConvention::GetParameterVariableForIncomingVariable(var, func);
}
```

If base fallback call signature is awkward, return `Variable()` for non-ARG vars after checking common patterns in other plugins/examples.

#### 1.3 Keep `YSCSymbolicLifter::Call` real-call based

Do not replace script calls with intrinsics.

Keep current pattern:

```cpp
SetRegister(ARGi, argExpr);
Call(ConstPointer(target));
Set temp from ReturnReg(i);
```

But after 1.1 and 1.2, BN should be able to recover call parameters better.

#### 1.4 Consider `CallStackAdjust` only if needed

`CallStackAdjust` exists but is probably not necessary because YSC call args are in virtual `ARGn` registers, not a native stack. Avoid unless testing shows BN is still modeling a stack adjustment incorrectly.

### Acceptance tests

After build/install/restart:

1. Inspect `sub_10001c78`:
   - Function type has 13 params for `ENTER 0xd, ...`.
   - Parameters are `arg1`..`arg13` or similar, backed by `ARG0`..`ARG12`.

2. Inspect `sub_10001c88`:
   - Calls to `sub_10001c78` show arguments in HLIL/pseudocode.
   - No large detached list of `arg1[i]` expressions immediately before a zero-arg call.

3. No noticeable analysis lag.

4. No `SetUserType` in diff.

5. No new fake sections or field symbol spam.

### Pros

- Uses Binary Ninja's intended call/calling convention model.
- Preserves real call graph and xrefs.
- Avoids user-state mutation.

### Cons

- If BN still cannot recover parameters from register assignments, there is no LLIL explicit-param call API to force this directly.
- May require follow-up investigation into MLIL mappings or inlining support.

## Phase 2: Safe local-address modeling

### Goal

Fix semantically wrong native args like:

```c
native_NETWORK_NETWORK_IS_HANDLE_VALID(nullptr, 0xd)
```

without introducing fake global memory.

### Chosen design

Add an intrinsic:

```c
ysc_local_ref(index)
```

Use this only when a `Value::Kind::LocalAddr` escapes into a call/native/store-through-pointer context.

Do **not** use a fake `LOCAL_FRAME` section.

Do **not** globally lower local addresses to constant pointers.

### Implementation plan

#### 2.1 Add intrinsic enum

In `YSCArchitecture.hpp`, extend:

```cpp
enum YSCIntrinsic
{
    Intrin_StringHash,
    Intrin_LocalRef,
    Intrin_MAX
};
```

Update `g_intrinNames`:

```cpp
"ysc_string_hash",
"ysc_local_ref"
```

#### 2.2 Add intrinsic type info

In `YSCArchitecture::GetIntrinsicInputs`:

```cpp
case Intrin_LocalRef:
    result.emplace_back("index", Type::IntegerType(4, false));
    break;
```

In `GetIntrinsicOutputs`:

```cpp
case Intrin_LocalRef:
    result.emplace_back(Type::PointerType(4, Type::VoidType()));
    break;
```

If `Type::PointerType(4, ...)` is not accepted in this style, use the architecture-aware overload used elsewhere in the project.

#### 2.3 Add helper in `YSCSymbolicLifter`

Add:

```cpp
BinaryNinja::ExprId EscapeValueExpr(const Value& value)
{
    if (value.kind == Kind::LocalAddr)
    {
        uint32_t tmp = CallResultTemp(m_il.GetCurrentAddress() ^ value.index); // or dedicated temp range
        m_il.AddInstruction(m_il.Intrinsic(
            {BinaryNinja::RegisterOrFlag::Register(tmp)},
            Intrin_LocalRef,
            {m_il.Const(4, value.index)}));
        return m_il.Register(4, tmp);
    }
    return value.expr;
}
```

Better: allocate a dedicated temp base, not `CallResultTemp`, to avoid collision:

```cpp
constexpr uint32_t YSC_LOCAL_REF_TEMP_BASE = 0x90000;
uint32_t LocalRefTemp(uint32_t index) { return LLIL_TEMP(YSC_LOCAL_REF_TEMP_BASE + index); }
```

#### 2.4 Use helper in call/native argument setup

In `YSCSymbolicLifter::Native` and `YSCSymbolicLifter::Call`:

Change:

```cpp
m_il.AddInstruction(m_il.SetRegister(4, ArgReg(argIndex), it->expr));
```

to:

```cpp
m_il.AddInstruction(m_il.SetRegister(4, ArgReg(argIndex), EscapeValueExpr(*it)));
```

Do **not** change `Load`, `Store`, `LoadN`, normal local load/store behavior yet.

### Acceptance tests

1. `native_NETWORK_NETWORK_IS_HANDLE_VALID(nullptr, 0xd)` becomes either:
   - `native_NETWORK_NETWORK_IS_HANDLE_VALID(ysc_local_ref(0), 0xd)`, or
   - a named temp derived from `ysc_local_ref(0)`.

2. No fake `LOCAL_FRAME` section appears.

3. No raw pointer to `0x70000000` appears.

4. No broad regression in unrelated functions.

### Pros

- Honest representation of VM local references.
- Bounded and local to escaping values.
- Avoids fake memory model.

### Cons

- Output is custom (`ysc_local_ref`) rather than perfect C `&local_0`.
- This is still a VM abstraction, not a native pointer.

## Phase 3: Global/runtime array cleanup without symbol spam

### Goal

Improve global array expressions while avoiding slow database mutations.

### Chosen design

Keep the existing base-symbol approach, but do not create per-field symbols from `LOAD_N`/`STORE_N`.

Improve expression shape using metadata carried in `Value`, not data variable creation.

### Current useful behavior

Current array lowering for a direct global/static address does:

```cpp
dataAddress = address.address + 4;
DefineRuntimeArrayDataAddress(address.address, dataAddress, stride);
return RuntimePointer(dataAddress) + stride * 4 * index;
```

This is directionally good because it removes the `1 + stride * i` header-cell arithmetic from the final expression.

### Problem areas

`IOFFSET_*` currently calls `DefineRuntimeArrayOffsetAlias` for `base.runtimeDataAddress`. This can create aliases. It should remain bounded and should not be extended to `LOAD_N`/`STORE_N`.

### Implementation plan

#### 3.1 Add structured address metadata to `Value`

Extend `Value` with optional metadata:

```cpp
struct RuntimeArrayRef
{
    uint64_t dataAddress = 0;
    uint32_t stride = 0;        // cells, not bytes
    int64_t fieldOffsetBytes = 0;
};

std::optional<RuntimeArrayRef> runtimeArray;
```

When `ArrayElementValue` sees `Kind::Address`, set:

```cpp
runtimeArray = RuntimeArrayRef{dataAddress, stride, 0};
```

When `IOffsetImm` / `IOffsetLoadImm` / `IOffsetStoreImm` sees `runtimeArray`, update `fieldOffsetBytes` in the metadata and build expression as:

```cpp
RuntimePointer(dataAddress) + strideBytes * index + fieldOffsetBytes
```

This may require storing the index expression in metadata too. If so:

```cpp
BinaryNinja::ExprId indexExpr;
```

But be careful: `ExprId` lifetime is within the IL function; metadata is only within the lifter, so this is fine.

#### 3.2 Do not create per-field aliases automatically

Remove or guard calls to `DefineRuntimeArrayOffsetAlias` in hot instruction paths if they generate too many symbols.

At minimum:

- Do not add new alias calls in `LOAD_N`/`STORE_N`.
- Consider capping existing `IOFFSET_*` alias creation:
  - max aliases per base, e.g. 16.
  - only constant offsets in range `0..0x100`.
  - only create if symbol does not already exist.

#### 3.3 Future optional command for struct inference

Do not implement automatically in this phase.

Later add a plugin command:

```text
YSC: Infer Runtime Array Structs for Current Function
```

This can create structs/types on user request only.

### Acceptance tests

1. `sub_10001fa0` should continue showing correct base global, ideally less raw absolute arithmetic.
2. No analysis lag.
3. Number of new symbols/data variables remains bounded.
4. No `LOAD_N`/`STORE_N` field alias fan-out.

### Pros

- Keeps semantics in IL.
- Avoids database pollution.
- Works with BN's existing simplifier/type propagation.

### Cons

- Won't immediately produce perfect `Global[i].field` C syntax.
- Full struct output requires later type inference.

## Phase 4: Regression harness

Before implementing any semantic change, snapshot current output.

### Required snapshots

Using MCP after Binary Ninja restart:

1. `sub_10001c78`
   - disassembly
   - MLIL
   - HLIL/decompile
   - function type / parameter count

2. `sub_10001c88`
   - MLIL
   - HLIL/decompile

3. `sub_10001fa0`
   - HLIL/decompile
   - check global arithmetic

4. Analysis logs
   - `YSC new-lift decision`
   - `symbolic-lift-failed`
   - `LLIL out of bounds`
   - `Cannot find source block`

### Suggested local artifact

Create a directory, not necessarily committed:

```text
/tmp/ysc-regression/YYYYMMDD-HHMM/
```

Save tool outputs there as text.

### Pass/fail criteria

Pass:

- Analysis completes in roughly comparable time to baseline.
- No new persistent lag.
- No new source-block/LLIL-out-of-bounds errors in the target functions.
- `sub_10001c88` call output improves or at least does not regress.
- Native local refs no longer render as `nullptr` after Phase 2.

Fail/revert:

- Noticeable global lag.
- Many new symbols/data vars.
- User-state mutation from auto-analysis.
- Worse output in unrelated functions.

## Build/install/test workflow

Build:

```bash
cd /home/tom/Documents/ysc-binja
cmake --build build -j$(nproc)
```

Install:

```bash
cd /home/tom/Documents/ysc-binja
cmake --install build
```

Then Binary Ninja must be restarted for plugin changes to load.

MCP may need refresh after restart -- ask the user for this.

## Implementation order

### Step A: Call convention/signature fix

Files:

- `src/CallingConvention/CallingConvention.hpp`
- `src/Architecture/YSCArchitecture.cpp`

Tasks:

1. Add ARG register mapping helper in calling convention.
2. Override parameter/incoming variable mapping.
3. Make `ApplyYSCFunctionType` idempotent.
4. Build/install/restart/test.

Expected improvement:

- `sub_10001c78` recognized as 13-arg function.
- Calls in `sub_10001c88` display args.

### Step B: Local reference intrinsic

Files:

- `src/Architecture/YSCArchitecture.hpp`
- `src/Architecture/YSCArchitecture.cpp`

Tasks:

1. Add `Intrin_LocalRef`.
2. Add intrinsic name/input/output definitions.
3. Add dedicated temp helper.
4. Add `EscapeValueExpr` helper.
5. Use in `Native` and `Call` arg setup only.
6. Build/install/restart/test.

Expected improvement:

- Local pointer natives no longer use `nullptr`.

### Step C: Runtime array metadata cleanup

Files:

- `src/Architecture/YSCArchitecture.cpp`

Tasks:

1. Add bounded metadata to `Value`.
2. Preserve array base/stride/index/field offset through `ARRAY` and `IOFFSET` operations.
3. Ensure no new data-symbol fan-out.
4. Cap or disable automatic field alias creation if it causes lag.
5. Build/install/restart/test.

Expected improvement:

- Better arithmetic shape for globals.
- No lag.

## Non-goals for this pass

- Perfect C struct output for globals.
- Automatic huge struct inference.
- Creating fake local stack/frame memory.
- Replacing real calls with `ysc_call_*` intrinsics.
- Rewriting the old fallback lifter.

## Notes for future work

If Phase 1 does not make calls render with arguments even after calling convention mapping and idempotent auto signatures, investigate:

1. Whether `FunctionLifterContext::CheckForInlinedCall` should be called around emitted calls. It may help BN track inlining/call metadata but is not obviously an argument-list solution.
2. Whether MLIL expression mappings can be used to associate ARG assignments with call parameter expressions.
3. Whether a custom workflow pass can rewrite MLIL call params, but this is significantly more invasive and should be avoided unless all architecture-level paths fail.

Keep all such work behind tests and avoid persistent user-state mutation.

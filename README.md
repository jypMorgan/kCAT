# WasmBuilder and Intermediate Representation.

WasmBuilder is a **Semantic WebAssembly Construction Kit**. Unlike a traditional assembler (e.g., `wat2wasm`) that maps text syntax directly to binary bytes, WasmBuilder maintains a full *in* memory **Intermediate Representation (IR)** of a WebAssembly module. Binary emission is a *backend concern* one of many possible consumers of the IR.

This design and three execution modes:
- **Binary Emission** (`ToBuffer`): Serialize the IR to a spec compliant Wasm binary.
- **Semantic Interpretation**: Walk the IR directly to execute or analyze the module without ever materializing bytes or relying on semantic level *not* bytes code level execution.
- **Structural Mutation**: Fuzz, optimize, or instrument the module by manipulating the IR graph, then reemit.

The inner principle is that **indices, types, and cross references are managed symbolically by the builder**, this eliminates an entire class of bugs likewise (index mismatches, invalid forward references) and provides a stable API for semantic tooling.

---

##  IR Layer And Semantic Graph.

The (Intermediate Representation) is a heterogeneous graph rooted at `WasmModuleBuilder`. Every entity is an owned, typed object with explicit relationships.

###  Module (`WasmModuleBuilder`)

The module is the single arena allocator and symbol table. It owns:

| Entity | Storage | Indexing |
|--------|---------|----------|
| **Types** | `std::vector<std::variant<WasmSig, WasmStructType, WasmArrayType, WasmContType>>` | Dense, ordered. Type indices are implicit positions. |
| **Functions** | `std::vector<std::unique_ptr<WasmFunctionBuilder>>` | Logical index = `num_imported_funcs_ + position`. |
| **Globals** | `std::vector<std::unique_ptr<WasmGlobalBuilder>>` | Logical index = `num_imported_globals_ + position`. |
| **Tables** | `std::vector<std::unique_ptr<WasmTableBuilder>>` | Logical index = `num_imported_tables_ + position`. |
| **Memories** | `std::vector<std::pair<uint32_t, uint32_t>>` | Logical index = `num_imported_memories_ + position`. |
| **Tags** | `std::vector<uint32_t>` (type indices) | Logical index = `num_imported_tags_ + position`. |
| **Imports** | `std::vector<std::variant<ImportEntry, std::unique_ptr<ImportGroupBuilder>>>` | Import indices are allocated eagerly via `AllocateImportIndex`. |
| **Exports** | `std::vector<ExportEntry>` | Symbolic `(name, kind, index)` triples. |
| **Data Segments** | `std::vector<DataSegment>` | Ordered list. |
| **Element Segments** | `std::vector<WasmElemSegment>` | Ordered list. |
| **String Refs** | `std::vector<std::string>` | Literal string pool for stringref proposal. |
| **Recursive Groups** | `std::vector<std::pair<uint32_t, uint32_t>>` | `(start_type_index, count)` pairs. |

 The module maintains a strict **append only, stable indexing** discipline. Once an entity is added, its logical index never changes. This allows builders to hold raw pointers back to the module without fear of invalidation.

###  Functions (`WasmFunctionBuilder`)

A function is not a blob of bytes, it is a structured object with:

- **Type Index**: A validated reference into the module's type table.
- **Locals**: A list of `(type, count)` pairs, where `type` is a `std::variant<int8_t, RefTypeBuilder>`. This is the *semantic* local declaration, not the raw compressed local encoding.
- **Body**: A `std::vector<uint8_t>` expression payload. While the body is currently stored as raw opcodes, it is treated as an opaque payload whose validity is checked (`CheckExpr`). In a full semantic pipeline, this payload is replaced by an AST node graph (e.g., `std::vector<std::unique_ptr<Instr>>`), and the `Binary` backend lowers it.
- **Local Names**: A run-length encoded name map (`std::variant<std::string, int>`) for the name section. This is pure metadata, not executable bytes.
- **Export Status**: Functions can export themselves (`ExportFunc`) or be exported under an alias (`ExportAs`), making the export edge explicit in the IR.

###  Types

And the type system is unified through `std::variant`:

```cpp
std::variant<WasmSig, WasmStructType, WasmArrayType, WasmContType>
```
This allows the type section to interleave function types, struct types, array types, and continuation types while preserving their semantic structure.

#### `WasmSig`
```cpp
struct WasmSig {
  std::vector<std::variant<int8_t, RefTypeBuilder>> params;
  std::vector<std::variant<int8_t, RefTypeBuilder>> results;
  bool is_final = true;
  bool is_shared = false;
  uint32_t supertype = kNoSuperType;
};
```
Parameters and results are **semantic type objects**, not encoded bytes. The `RefTypeBuilder` encodes reference type modifiers (nullable, shared, exact) as C++ method calls, *not* as **LEB128** bytes.


#### `WasmStructType` And `WasmArrayType`

 (Garbage Collection) types are first class. Structs carry fields with mutability flags. Arrays carry element type and mutability. Both support supertyping, finality, and sharedness exactly as the GC proposal specifies, but represented as C++ structs.

### Imports And Exports

Imports are **symbolic** until emission:

```cpp
struct ImportEntry {
  std::string module;
  std::string name;
  uint8_t kind;
 /* kind specific payload.. */
};
```
The user never writes a raw import index. The builder calls `AllocateImportIndex(kind);` which assigns the next available index for that kind and increments the appropriate counter (`num_imported_funcs_`, i,e.. and e,g..). This guarantees that imported indices are dense and correctly ordered per the Wasm spec (functions, tables, memories, globals, tags).

Exports are similarly symbolic:

```cpp
struct ExportEntry {
  std::string name;
  uint8_t kind;
  uint32_t index;  /* resolved logical index */
};
```
Because indices are stable, an export can be added at any time after the target entity is created.

###  Data And Element Segments

Segments are **semantic objects**, not raw section bytes.

- **DataSegment**: `is_active`, `mem_index`, `offset` (init expression), `data`.
- **WasmElemSegment**: `table`, `offset`, `type`, `elements` (where each element is either a function index or an init expression).

The builder tells apart active, passive, and declarative elements as well as expression based versus index based elements at the type level, instead of using parsing flags

---

##  Type System

###  Value Types vs Reference Types

WasmBuilder uses a `std::variant<int8_t, RefTypeBuilder>` to represent any type that can appear in a value context.

- `int8_t` encodes simple value types (`kWasmI32`, `kWasmI64`, `kWasmF32`, `kWasmF64`, `kWasmS128`, `kWasmI8`, `kWasmI16`, `kWasmF16`) and abstract reference types (`kWasmFuncRef`, `kWasmExternRef`, etc.).
- `RefTypeBuilder` encodes *constructed* reference types with modifiers: nullable, shared, exact, and a specific heap type index.

The `int8_t` branch handles the common case (single-byte type encodings) with zero overhead. The `RefTypeBuilder` branch handles the GC and stringref proposals' more complex type constructors.

###  Recursive Type Groups

The builder supports the Wasm GC recursive type group proposal through an explicit stack:

```cpp
WasmModuleBuilder& StartRecGroup();
WasmModuleBuilder& EndRecGroup();
```

During emission, the type section checks `rec_groups_` to emit `kWasmRecursiveTypeGroupForm` markers at the correct boundaries. The user does not need to compute group sizes or offsets manually.

###  Subtyping & Shared Types

`WasmSig`, `WasmStructType`, and `WasmArrayType` all carry:
- `supertype`: An index into the type section, or `kNoSuperType`.
- `is_final`: Whether the type admits further subtypes.
- `is_shared`: Whether the type is shared (for shared-everything threads).

The emitter automatically generates `kWasmSubtypeForm` / `kWasmSubtypeFinalForm` prefixes when `supertype != kNoSuperType`.

---

## Binary Emission Layer

The `Binary` class is a low level byte serializer. It is **stateless with respect to Wasm semantics** it only knows how to write LEB128, sections, and strings. The *semantic* decisions (what to write, in what order) are made by `WasmModuleBuilder`.

### The Binary Serializer

```cpp
class Binary {
  void EmitU32v(uint32_t val);   // LEB128 unsigned
  void EmitS32v(int32_t val);    // LEB128 signed
  void EmitType(std::variant<int8_t, RefTypeBuilder> type); // semantic -> bytes
  void EmitSection(uint8_t code, std::function<void(Binary&)> gen); // section wrapper
};
```

`EmitSection` is the hard abstraction. It measures the generated content, emits the section code and size, then emits the content. This guarantees correct section framing without manual size precomputation.

###  Section Ordering & Spec Compliance

`ToBuffer()` emits sections in the **mandatory order** defined by the Wasm spec:

```
1. Header (magic + version)
2. Type Section
3. Import Section
4. Function Section
5. Table Section
6. Memory Section
7. Tag Section
8. StringRef Section
9. Global Section
10. Export Section
11. Start Section
12. Element Section
13. Data Count Section (conditional)
14. Code Section
15. Data Section
16. Explicit / Custom Sections
17. Name Section (custom)
18. Metadata Sections (custom)
```

The builder conditionally omits empty sections. The Data Count Section is emitted only if passive data segments exist, as required by the bulk memory proposal.

### LEB128 & Encoding Details

All integer encoding uses bounded loops with `CHECK(false)` on overflow. This is a **defensive design**: if a value cannot be encoded in the maximum LEB128 length (5 bytes for 32-bit, 10 for 64-bit), the process aborts rather than emitting malformed bytes.

### Lazy, vs  Eager Emission.

Emission is **lazy** (triggered by `ToBuffer`) but the IR is **eagerly validated**. Every mutating operation (`AddFunction`, `AddGlobal`, etc.) performs structural checks immediately. This means:

- `ToBuffer` is a pure function over a valid IR.
- It is infeasible to emit a module with an out-of-bounds type index or invalid local type, because the builder would have `CHECK` failed during construction.

---

##  Semantic Model

###  Why not just bytes?

A byte array is an **lossy representation** for tooling. Once you lower a module to bytes, you lose:
- Symbolic names (functions, locals, globals).
- Structural boundaries (where does the type section end? which type belongs to which function?).
- Type information for locals and expressions (bytes must be re-parsed to recover the stack type of each instruction).
- Mutation safety (patching bytes requires re encoding offsets and indices).

WasmBuilder retains all of this. The binary is a *view* of the IR.

###  IR as Executable AST

While function bodies are currently stored as byte sequences, the surrounding structure is a fully executable semantic graph. A semantic engine can:

1. **Walk the module** without parsing, iterate `functions()`, `globals()`, `types()` directly.
2. **Resolve cross references** by index: `func->type_index()` is a direct lookup into `types()`.
3. **Interpret init expressions**: `WasmModuleBuilder::DefaultFor(type)` generates semantic init expressions for any type.
4. **Validate types structurally**: `ValidateTypeVariant` checks types without emitting them.

To achieve full semantical execution (e.g., for a fuzzer or interpreter), the function body payload would be replaced by an instruction AST. The `Binary` backend would then become a **lowering pass** from AST to bytes. The existing `WasmFunctionBuilder` API is designed to accommodate this:
`AddBody` is a narrow interface that can be extended to accept `std::vector<std::unique_ptr<Instr>>` alongside the raw byte fallback.

###  Mutation & Fuzzing Surface
Because the IR is a graph of C++ objects, mutation is straightforward:
- **Type mutation**: Replace a `WasmSig` in `types_` (requires index stability).
- **Function mutation**: Replace a function's body, add locals, or change its export status.
- **Import mutation**: Swap an `ImportEntry`'s module/name without re-indexing.
- **Segment mutation**: Resize data segments or change element segment modes.

All mutations are **local** they do not require re encoding the entire module to fix up offsets. Only `ToBuffer` performs global layout.

### Metadata & Instrumentation Hooks
The builder includes non standard metadata sections that demonstrate its suitability as a compiler/fuzzer IR:

- **Compilation Priority**: Hints for tiered compilation (`SetCompilationPriority`).
- **Instruction Frequencies**: Per function basic block hotness (`SetInstructionFrequencies`).
- **Call Targets**: Per call site profiling data (`SetCallTargets`).

These are stored as C++ structs in the IR and emitted as custom sections. A semantic execution engine can read them directly without parsing custom section bytes.

---

## Validation & Safety

###   (`CHECK`)

The builder uses a `CHECK(cond)` macro that prints the file, line, and failed condition to `stderr`, prints a backtrace (via `execinfo.h` on supported platforms), and calls `std::abort()`. Every invariant violation is a hard stop.

### Bounds & Limits
All growth operations check against hard limits:

```cpp
CHECK(types_.size() < kMaxTypes);        // 200,000
CHECK(functions_.size() < kMaxFunctions); // 200,000
CHECK(globals_.size() < kMaxGlobals);    // 100,000
// ... etc and more..
```

These limits prevent unbounded memory growth during fuzzing or generation. They are set well above the Wasm spec's own limits but low enough to prevent OOM.

###  Stack Depth Guard

`ToBuffer` uses a `thread_local` stack depth counter to prevent infinite recursion during emission (e.g., a malformed recursive type group or a buggy custom section generator). This is a defense-in-depth measure for generator robustness.

### Local Bounds

`WasmFunctionBuilder::AddLocals` checks the **cumulative** local count against `kMaxLocalsPerFunction`, not just the current declaration. This prevents subtle overflow bugs where many small local declarations sum to an invalid total.

---

##  Extensibility

###  Custom Sections

`AddCustomSection(name, bytes)` and `AddExplicitSection(bytes)` allow arbitrary payload injection. The difference:
- `AddCustomSection` wraps the payload in the standard custom section framing (name string + bytes).
- `AddExplicitSection` emits raw bytes directly, for spec extensions or testing.

###  Import Groups

The compact import proposal (grouping imports by module or by module+type) is supported via `ImportGroupBuilder`. This is an advanced emission optimization that reduces binary size for modules with many imports from the same module. The IR represents the group as a distinct object, and the emitter handles the compact encoding transparently.

---

##  Implementation Notes

###  Memory Management

All complex entities are owned by `WasmModuleBuilder` via `std::unique_ptr`. Builders return raw pointers (`WasmFunctionBuilder*`) that are guaranteed stable because the owning vectors never reallocate existing elements (only append). This is a **non-owning, stable-pointer** pattern common in compiler ASTs.

###  Thread Safety

The `StackDepthGuard` uses `thread_local` storage. Otherwise, `WasmModuleBuilder` is **not thread-safe**. It is intended to be used by a single generator thread (e.g., a fuzzer core or compiler frontend).

###  Performance Characteristics

- **Type checking**: `O(1)` per type via `std::visit` and `std::holds_alternative`.
- **Index lookup**: `O(1)` via direct array indexing.
- **Binary emission**: `O(N)` where N is total module size. `Binary::EnsureSpace` uses exponential growth to amortize allocation cost.
- **Memory overhead**: Roughly 2-3x the final binary size due to C++ object overhead (strings, vectors, vtables). This is acceptable for a generator IR.

---

 Usage using C++, as this would produce a JS instead.

```cpp
// 1. Create module 
cat::wasm_builder::WasmModuleBuilder module;

// 2. Define types
uint32_t t_add = module.AddType(
    cat::wasm_builder::MakeSig({kWasmI32, kWasmI32}, {kWasmI32}));

// 3. Import a function
uint32_t imp = module.AddImport("env", "host_func",
    cat::wasm_builder::MakeSig({kWasmI32}, {kWasmI32}));

// 4. Define a function with semantic locals
auto* func = module.AddFunction("add", t_add, {"a", "b"});
func->AddLocals(kWasmI32, 2, {"tmp1", "tmp2"})
    .AddBody({ /* ... semantic or encoded body ... */ })
    .ExportFunc();

//  Add a global with semantic type and default init
auto* glob = module.AddGlobal(kWasmI32, true, false);
glob->ExportAs("counter");

// 6. Emit (or interpret)
auto bytes = module.ToBuffer();  // Binary backend
// OR
// interpret::Execute(module);  // Semantic backend
```

In this pipeline, the user never touches a raw index, a LEB128 encoder, or a section offset. The IR is the program.

---

*End of Specification*

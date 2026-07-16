#ifndef BUILDER_WASM_BUILDER_H_
#define BUILDER_WASM_BUILDER_H_

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#include <unistd.h>
#define WASM_BUILDER_HAS_BACKTRACE 1
#endif
#endif

#ifndef WASM_BUILDER_HAS_BACKTRACE
#define WASM_BUILDER_HAS_BACKTRACE 0
#endif

namespace cat {
namespace wasm_builder {
namespace internal {

inline void PrintCheckBacktrace() {
#if WASM_BUILDER_HAS_BACKTRACE
  void* trace[64];
  int count = backtrace(trace, 64);
  backtrace_symbols_fd(trace, count, STDERR_FILENO);
#else
  (void)0;
#endif
}

}  // namespace internal
}  // namespace wasm_builder
}  // namespace cat

#ifndef CHECK
#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::fprintf(stderr, \
                   "\n#\n" \
                   "# Fatal error in %s, line %d\n" \
                   "# Check failed: %s.\n" \
                   "#\n", \
                   __FILE__, __LINE__, #cond); \
      ::cat::wasm_builder::internal::PrintCheckBacktrace(); \
      std::abort(); \
    } \
  } while (0)
#endif

constexpr uint8_t kUnknownSectionCode = 0;
constexpr uint8_t kTypeSectionCode = 1;
constexpr uint8_t kImportSectionCode = 2;
constexpr uint8_t kFunctionSectionCode = 3;
constexpr uint8_t kTableSectionCode = 4;
constexpr uint8_t kMemorySectionCode = 5;
constexpr uint8_t kGlobalSectionCode = 6;
constexpr uint8_t kExportSectionCode = 7;
constexpr uint8_t kStartSectionCode = 8;
constexpr uint8_t kElementSectionCode = 9;
constexpr uint8_t kCodeSectionCode = 10;
constexpr uint8_t kDataSectionCode = 11;
constexpr uint8_t kDataCountSectionCode = 12;
constexpr uint8_t kTagSectionCode = 13;
constexpr uint8_t kStringRefSectionCode = 14;
constexpr uint8_t kLastKnownSectionCode = 14;

constexpr uint8_t kModuleNameCode = 0;
constexpr uint8_t kFunctionNamesCode = 1;
constexpr uint8_t kLocalNamesCode = 2;

// Type forms
constexpr uint8_t kWasmSharedTypeForm = 0x65;
constexpr uint8_t kWasmFunctionTypeForm = 0x60;
constexpr uint8_t kWasmStructTypeForm = 0x5f;
constexpr uint8_t kWasmArrayTypeForm = 0x5e;
constexpr uint8_t kWasmContTypeForm = 0x5d;
constexpr uint8_t kWasmSubtypeForm = 0x50;
constexpr uint8_t kWasmSubtypeFinalForm = 0x4f;
constexpr uint8_t kWasmRecursiveTypeGroupForm = 0x4e;
constexpr uint8_t kWasmDescriptorTypeForm = 0x4d;
constexpr uint8_t kWasmDescribesTypeForm = 0x4c;

constexpr uint32_t kNoSuperType = 0xFFFFFFFF;

// Limit flags
constexpr uint8_t kLimitsNoMaximum = 0x00;
constexpr uint8_t kLimitsWithMaximum = 0x01;
constexpr uint8_t kLimitsSharedNoMaximum = 0x02;
constexpr uint8_t kLimitsSharedWithMaximum = 0x03;
constexpr uint8_t kLimitsMemory64NoMaximum = 0x04;
constexpr uint8_t kLimitsMemory64WithMaximum = 0x05;
constexpr uint8_t kLimitsMemory64SharedNoMaximum = 0x06;
constexpr uint8_t kLimitsMemory64SharedWithMaximum = 0x07;

// Segment flags
constexpr uint8_t kActiveNoIndex = 0;
constexpr uint8_t kPassive = 1;
constexpr uint8_t kActiveWithIndex = 2;
constexpr uint8_t kDeclarative = 3;
constexpr uint8_t kPassiveWithElements = 5;
constexpr uint8_t kDeclarativeWithElements = 7;

// Function declaration flags
constexpr uint8_t kDeclFunctionName = 0x01;
constexpr uint8_t kDeclFunctionImport = 0x02;
constexpr uint8_t kDeclFunctionLocals = 0x04;
constexpr uint8_t kDeclFunctionExport = 0x08;

// External kinds
constexpr uint8_t kExternalFunction = 0;
constexpr uint8_t kExternalTable = 1;
constexpr uint8_t kExternalMemory = 2;
constexpr uint8_t kExternalGlobal = 3;
constexpr uint8_t kExternalTag = 4;
constexpr uint8_t kExternalExactFunction = 32;

// Compact import encodings
constexpr uint8_t kCompactImportByModule = 0x7F;
constexpr uint8_t kCompactImportByModuleAndType = 0x7E;
constexpr uint8_t kIndividualImportEncoding = 0;

// Exception attribute
constexpr uint8_t kExceptionAttribute = 0;

// Atomic ordering
constexpr uint8_t kAtomicSeqCst = 0;
constexpr uint8_t kAtomicAcqRel = 1;

// Value types
constexpr int8_t kWasmVoid = 0x40;
constexpr int8_t kWasmI32 = 0x7f;
constexpr int8_t kWasmI64 = 0x7e;
constexpr int8_t kWasmF32 = 0x7d;
constexpr int8_t kWasmF64 = 0x7c;
constexpr int8_t kWasmS128 = 0x7b;
constexpr int8_t kWasmI8 = 0x78;
constexpr int8_t kWasmI16 = 0x77;
constexpr int8_t kWasmF16 = 0x76;

// Reference types (negative to distinguish from positive type indices)
constexpr int8_t kWasmNullFuncRef = -0x0d;
constexpr int8_t kWasmNullExternRef = -0x0e;
constexpr int8_t kWasmNullRef = -0x0f;
constexpr int8_t kWasmFuncRef = -0x10;
constexpr int8_t kWasmAnyFunc = kWasmFuncRef;  // Alias
constexpr int8_t kWasmExternRef = -0x11;
constexpr int8_t kWasmAnyRef = -0x12;
constexpr int8_t kWasmEqRef = -0x13;
constexpr int8_t kWasmI31Ref = -0x14;
constexpr int8_t kWasmStructRef = -0x15;
constexpr int8_t kWasmArrayRef = -0x16;
constexpr int8_t kWasmExnRef = -0x17;
constexpr int8_t kWasmNullExnRef = -0x0c;
constexpr int8_t kWasmStringRef = -0x19;
constexpr int8_t kWasmStringViewWtf8 = -0x1a;
constexpr int8_t kWasmStringViewWtf16 = -0x20;
constexpr int8_t kWasmStringViewIter = -0x1f;
constexpr int8_t kWasmWaitqueueRef = -0x24;
constexpr int8_t kWasmNullWaitqueueRef = -0x25;
constexpr int8_t kWasmContRef = -0x18;
constexpr int8_t kWasmNullContRef = -0x0b;

// Mask for encoding negative types as single bytes
constexpr uint8_t kLeb128Mask = 0x7f;

// Reference type opcodes
constexpr uint8_t kWasmRefNull = 0x63;
constexpr uint8_t kWasmRef = 0x64;
constexpr uint8_t kWasmExact = 0x62;

// Prefix opcodes
constexpr uint8_t kGCPrefix = 0xfb;
constexpr uint8_t kNumericPrefix = 0xfc;
constexpr uint8_t kSimdPrefix = 0xfd;
constexpr uint8_t kAtomicPrefix = 0xfe;

// Expression opcodes
constexpr uint8_t kExprEnd = 0x0b;
constexpr uint8_t kExprRefNull = 0xd0;
constexpr uint8_t kExprI32Const = 0x41;
constexpr uint8_t kExprI64Const = 0x42;
constexpr uint8_t kExprF32Const = 0x43;
constexpr uint8_t kExprF64Const = 0x44;
constexpr uint8_t kExprReturn = 0x0f;
constexpr uint8_t kExprUnreachable = 0x00;
constexpr uint8_t kExprNop = 0x01;
constexpr uint8_t kExprBlock = 0x02;
constexpr uint8_t kExprLoop = 0x03;
constexpr uint8_t kExprIf = 0x04;
constexpr uint8_t kExprElse = 0x05;
constexpr uint8_t kExprTry = 0x06;
constexpr uint8_t kExprTryTable = 0x1f;
constexpr uint8_t kExprThrowRef = 0x0a;
constexpr uint8_t kExprCatch = 0x07;
constexpr uint8_t kExprThrow = 0x08;
constexpr uint8_t kExprRethrow = 0x09;
constexpr uint8_t kExprCatchAll = 0x19;
constexpr uint8_t kExprBr = 0x0c;
constexpr uint8_t kExprBrIf = 0x0d;
constexpr uint8_t kExprBrTable = 0x0e;
constexpr uint8_t kExprCallFunction = 0x10;
constexpr uint8_t kExprCallIndirect = 0x11;
constexpr uint8_t kExprReturnCall = 0x12;
constexpr uint8_t kExprReturnCallIndirect = 0x13;
constexpr uint8_t kExprCallRef = 0x14;
constexpr uint8_t kExprReturnCallRef = 0x15;
constexpr uint8_t kExprNopForTestingUnsupportedInLiftoff = 0x16;
constexpr uint8_t kExprDelegate = 0x18;
constexpr uint8_t kExprDrop = 0x1a;
constexpr uint8_t kExprSelect = 0x1b;
constexpr uint8_t kExprSelectWithType = 0x1c;
constexpr uint8_t kExprLocalGet = 0x20;
constexpr uint8_t kExprLocalSet = 0x21;
constexpr uint8_t kExprLocalTee = 0x22;
constexpr uint8_t kExprGlobalGet = 0x23;
constexpr uint8_t kExprGlobalSet = 0x24;
constexpr uint8_t kExprTableGet = 0x25;
constexpr uint8_t kExprTableSet = 0x26;
constexpr uint8_t kExprI32LoadMem = 0x28;
constexpr uint8_t kExprI64LoadMem = 0x29;
constexpr uint8_t kExprF32LoadMem = 0x2a;
constexpr uint8_t kExprF64LoadMem = 0x2b;
constexpr uint8_t kExprI32StoreMem = 0x36;
constexpr uint8_t kExprI64StoreMem = 0x37;
constexpr uint8_t kExprF32StoreMem = 0x38;
constexpr uint8_t kExprF64StoreMem = 0x39;
constexpr uint8_t kExprMemorySize = 0x3f;
constexpr uint8_t kExprMemoryGrow = 0x40;
constexpr uint8_t kExprRefIsNull = 0xd1;
constexpr uint8_t kExprRefFunc = 0xd2;
constexpr uint8_t kExprRefEq = 0xd3;
constexpr uint8_t kExprRefAsNonNull = 0xd4;
constexpr uint8_t kExprBrOnNull = 0xd5;
constexpr uint8_t kExprBrOnNonNull = 0xd6;
constexpr uint8_t kExprContNew = 0xe0;
constexpr uint8_t kExprContBind = 0xe1;
constexpr uint8_t kExprSuspend = 0xe2;
constexpr uint8_t kExprResume = 0xe3;
constexpr uint8_t kExprResumeThrow = 0xe4;
constexpr uint8_t kExprResumeThrowRef = 0xe5;
constexpr uint8_t kExprSwitch = 0xe6;

// GC opcodes
constexpr uint8_t kExprStructNew = 0x00;
constexpr uint8_t kExprStructNewDefault = 0x01;
constexpr uint8_t kExprStructGet = 0x02;
constexpr uint8_t kExprStructGetS = 0x03;
constexpr uint8_t kExprStructGetU = 0x04;
constexpr uint8_t kExprStructSet = 0x05;
constexpr uint8_t kExprArrayNew = 0x06;
constexpr uint8_t kExprArrayNewDefault = 0x07;
constexpr uint8_t kExprArrayNewFixed = 0x08;
constexpr uint8_t kExprArrayNewData = 0x09;
constexpr uint8_t kExprArrayNewElem = 0x0a;
constexpr uint8_t kExprArrayGet = 0x0b;
constexpr uint8_t kExprArrayGetS = 0x0c;
constexpr uint8_t kExprArrayGetU = 0x0d;
constexpr uint8_t kExprArraySet = 0x0e;
constexpr uint8_t kExprArrayLen = 0x0f;
constexpr uint8_t kExprArrayFill = 0x10;
constexpr uint8_t kExprArrayCopy = 0x11;
constexpr uint8_t kExprArrayInitData = 0x12;
constexpr uint8_t kExprArrayInitElem = 0x13;
constexpr uint8_t kExprRefTest = 0x14;
constexpr uint8_t kExprRefTestNull = 0x15;
constexpr uint8_t kExprRefCast = 0x16;
constexpr uint8_t kExprRefCastNull = 0x17;
constexpr uint8_t kExprBrOnCast = 0x18;
constexpr uint8_t kExprBrOnCastFail = 0x19;
constexpr uint8_t kExprAnyConvertExtern = 0x1a;
constexpr uint8_t kExprExternConvertAny = 0x1b;
constexpr uint8_t kExprRefI31 = 0x1c;
constexpr uint8_t kExprI31GetS = 0x1d;
constexpr uint8_t kExprI31GetU = 0x1e;
constexpr uint8_t kExprRefI31Shared = 0x1f;
constexpr uint8_t kExprStructNewDesc = 0x20;
constexpr uint8_t kExprStructNewDefaultDesc = 0x21;
constexpr uint8_t kExprRefGetDesc = 0x22;
constexpr uint8_t kExprRefCastDescEq = 0x23;
constexpr uint8_t kExprRefCastDescEqNull = 0x24;
constexpr uint8_t kExprBrOnCastDescEq = 0x25;
constexpr uint8_t kExprBrOnCastDescEqFail = 0x26;
constexpr uint8_t kExprRefCastNop = 0x4c;

// Numeric opcodes
constexpr uint8_t kExprI32SConvertSatF32 = 0x00;
constexpr uint8_t kExprI32UConvertSatF32 = 0x01;
constexpr uint8_t kExprI32SConvertSatF64 = 0x02;
constexpr uint8_t kExprI32UConvertSatF64 = 0x03;
constexpr uint8_t kExprI64SConvertSatF32 = 0x04;
constexpr uint8_t kExprI64UConvertSatF32 = 0x05;
constexpr uint8_t kExprI64SConvertSatF64 = 0x06;
constexpr uint8_t kExprI64UConvertSatF64 = 0x07;
constexpr uint8_t kExprMemoryInit = 0x08;
constexpr uint8_t kExprDataDrop = 0x09;
constexpr uint8_t kExprMemoryCopy = 0x0a;
constexpr uint8_t kExprMemoryFill = 0x0b;
constexpr uint8_t kExprTableInit = 0x0c;
constexpr uint8_t kExprElemDrop = 0x0d;
constexpr uint8_t kExprTableCopy = 0x0e;
constexpr uint8_t kExprTableGrow = 0x0f;
constexpr uint8_t kExprTableSize = 0x10;
constexpr uint8_t kExprTableFill = 0x11;
constexpr uint8_t kExprI64Add128 = 0x13;
constexpr uint8_t kExprI64Sub128 = 0x14;
constexpr uint8_t kExprI64MulWideS = 0x15;
constexpr uint8_t kExprI64MulWideU = 0x16;

// Trap codes
constexpr int kTrapUnreachable = 0;
constexpr int kTrapMemOutOfBounds = 1;
constexpr int kTrapDivByZero = 2;
constexpr int kTrapDivUnrepresentable = 3;
constexpr int kTrapRemByZero = 4;
constexpr int kTrapFloatUnrepresentable = 5;
constexpr int kTrapTableOutOfBounds = 6;
constexpr int kTrapNullFunc = 7;
constexpr int kTrapFuncSigMismatch = 8;
constexpr int kTrapUnalignedAccess = 9;
constexpr int kTrapDataSegmentOutOfBounds = 10;
constexpr int kTrapElementSegmentOutOfBounds = 11;
constexpr int kTrapRethrowNull = 12;
constexpr int kTrapArrayTooLarge = 13;
constexpr int kTrapArrayOutOfBounds = 14;
constexpr int kTrapNullDereference = 15;
constexpr int kTrapIllegalCast = 16;
constexpr int kTrapCount = 17;

// Exception handling
constexpr uint8_t kCatchNoRef = 0x0;
constexpr uint8_t kCatchRef = 0x1;
constexpr uint8_t kCatchAllNoRef = 0x2;
constexpr uint8_t kCatchAllRef = 0x3;

// Stack switching
constexpr uint8_t kOnSuspend = 0x0;
constexpr uint8_t kOnSwitch = 0x1;

// Atomic wait results
constexpr int kAtomicWaitOk = 0;
constexpr int kAtomicWaitNotEqual = 1;
constexpr int kAtomicWaitTimedOut = 2;

// Header
constexpr uint8_t kWasmH0 = 0;
constexpr uint8_t kWasmH1 = 0x61;
constexpr uint8_t kWasmH2 = 0x73;
constexpr uint8_t kWasmH3 = 0x6d;
constexpr uint8_t kWasmV0 = 0x1;
constexpr uint8_t kWasmV1 = 0;
constexpr uint8_t kWasmV2 = 0;
constexpr uint8_t kWasmV3 = 0;
constexpr size_t kHeaderSize = 8;
constexpr size_t kPageSize = 65536;
constexpr size_t kSpecMaxPages = 65536;
constexpr size_t kMaxVarInt32Size = 5;
constexpr size_t kMaxVarInt64Size = 10;
constexpr size_t kSpecMaxFunctionParams = 1000;
constexpr uint8_t kDeclNoLocals = 0;

namespace cat {
namespace wasm_builder {

// Forward declarations
class WasmModuleBuilder;
class WasmFunctionBuilder;
class WasmGlobalBuilder;
class WasmTableBuilder;
class ImportGroupBuilder;
class Binary;

// RefTypeBuilder - MUST be defined before it's used in variant
class RefTypeBuilder {
 public:
  RefTypeBuilder(uint8_t opcode, int32_t heap_type)
      : opcode_(opcode), heap_type_(heap_type) {}

  RefTypeBuilder& nullable() {
    opcode_ = kWasmRefNull;
    return *this;
  }
  RefTypeBuilder& shared() {
    is_shared_ = true;
    return *this;
  }
  RefTypeBuilder& exact() {
    is_exact_ = true;
    return *this;
  }

  uint8_t opcode() const { return opcode_; }
  int32_t heap_type() const { return heap_type_; }
  bool is_shared() const { return is_shared_; }
  bool is_exact() const { return is_exact_; }

 private:
  uint8_t opcode_;
  int32_t heap_type_;
  bool is_shared_ = false;
  bool is_exact_ = false;
};

// Helper constructors
inline RefTypeBuilder wasmRefNullType(int32_t heap_type) {
  return RefTypeBuilder(kWasmRefNull, heap_type);
}
inline RefTypeBuilder wasmRefType(int32_t heap_type) {
  return RefTypeBuilder(kWasmRef, heap_type);
}

// Type system
struct WasmSig {
  std::vector<std::variant<int8_t, RefTypeBuilder>> params;
  std::vector<std::variant<int8_t, RefTypeBuilder>> results;
  bool is_final = true;
  bool is_shared = false;
  uint32_t supertype = kNoSuperType;
};

struct WasmField {
  std::variant<int8_t, RefTypeBuilder> type;
  bool mutability = false;
};

struct WasmStructType {
  std::vector<WasmField> fields;
  bool is_final = false;
  bool is_shared = false;
  uint32_t supertype = kNoSuperType;
  std::optional<uint32_t> descriptor;
  std::optional<uint32_t> describes;
};

struct WasmArrayType {
  std::variant<int8_t, RefTypeBuilder> type;
  bool mutability = true;
  bool is_final = false;
  bool is_shared = false;
  uint32_t supertype = kNoSuperType;
};

struct WasmContType {
  uint32_t type_index = 0;
};

// Import entry
struct ImportEntry {
  std::string module;
  std::string name;
  uint8_t kind = kExternalFunction;
  uint8_t encoding = kIndividualImportEncoding;
  uint32_t type_index = 0;
  std::variant<int8_t, RefTypeBuilder> type;
  bool mutable_ = false;
  bool shared = false;
  uint32_t initial = 0;
  std::optional<uint32_t> maximum;
  bool is_memory64 = false;
  bool is_table64 = false;
};

// Export entry
struct ExportEntry {
  std::string name;
  uint8_t kind = kExternalFunction;
  uint32_t index = 0;
};

// Data segment
struct DataSegment {
  bool is_active = false;
  bool is_shared = false;
  uint32_t mem_index = 0;
  std::vector<uint8_t> offset;
  std::vector<uint8_t> data;
};

// Element segment
struct WasmElemSegment {
  std::optional<uint32_t> table;
  std::vector<uint8_t> offset;
  std::optional<std::variant<int8_t, RefTypeBuilder>> type;
  std::vector<std::variant<uint32_t, std::vector<uint8_t>>> elements;
  bool is_decl = false;
  bool is_shared = false;

  bool IsActive() const { return table.has_value(); }
  bool IsPassive() const { return !table.has_value() && !is_decl; }
  bool IsDeclarative() const { return !table.has_value() && is_decl; }
  bool ExpressionsAsElements() const { return type.has_value(); }
};

// Instruction frequency
struct InstructionFrequency {
  uint32_t offset = 0;
  uint8_t frequency = 0;
};

// Call target
struct CallTarget {
  uint32_t function_index = 0;
  uint32_t frequency_percent = 0;
};

struct CallTargetAtOffset {
  uint32_t offset = 0;
  std::vector<CallTarget> targets;
};

// Compilation priority
struct CompilationPriority {
  uint32_t compilation_priority = 0;
  std::optional<uint32_t> optimization_priority;
};

// Binary encoder
class Binary {
 public:
  Binary();
  ~Binary() = default;
  Binary(const Binary&) = delete;
  Binary& operator=(const Binary&) = delete;
  Binary(Binary&&) = default;
  Binary& operator=(Binary&&) = default;

  void Reset();
  void EnsureSpace(size_t needed);

  void EmitU8(uint8_t val);
  void EmitU16(uint16_t val);
  void EmitU32(uint32_t val);
  void EmitU32v(uint32_t val);
  void EmitU64v(uint64_t val);
  void EmitS32v(int32_t val);
  void EmitS64v(int64_t val);
  void EmitBytes(std::span<const uint8_t> data);
  void EmitString(std::string_view str);
  void EmitType(std::variant<int8_t, RefTypeBuilder> type);
  void EmitHeapType(int32_t heap_type);
  void EmitInitExpr(std::span<const uint8_t> expr);
  void EmitHeader();
  void EmitSection(uint8_t section_code,
                   const std::function<void(Binary&)>& generator);

  std::vector<uint8_t> TruncBuffer() const;
  size_t length() const { return buffer_.size(); }
  const std::vector<uint8_t>& buffer() const { return buffer_; }
  std::vector<uint8_t>& buffer() { return buffer_; }

 private:
  std::vector<uint8_t> buffer_;
};

// WasmFunctionBuilder
class WasmFunctionBuilder {
 public:
  WasmFunctionBuilder(WasmModuleBuilder* module, std::string name,
                      uint32_t type_index,
                      std::vector<std::variant<std::string, int>> arg_names);
  ~WasmFunctionBuilder() = default;
  WasmFunctionBuilder(const WasmFunctionBuilder&) = delete;
  WasmFunctionBuilder& operator=(const WasmFunctionBuilder&) = delete;

  WasmFunctionBuilder& ExportAs(const std::string& name);
  WasmFunctionBuilder& ExportFunc();
  WasmFunctionBuilder& AddBody(std::span<const uint8_t> body);
  WasmFunctionBuilder& AddBodyWithEnd(std::span<const uint8_t> body);
  WasmFunctionBuilder& AddLocals(std::variant<int8_t, RefTypeBuilder> type,
                                 uint32_t count,
                                 const std::vector<std::string>& names);

  uint32_t NumLocalNames() const;
  WasmFunctionBuilder& End() { return *this; }

  void set_index(uint32_t idx) { index_ = idx; }

  // Accessors
  WasmModuleBuilder* module() const { return module_; }
  const std::string& name() const { return name_; }
  uint32_t type_index() const { return type_index_; }
  uint32_t index() const { return index_; }
  const std::vector<uint8_t>& body() const { return body_; }
  const std::vector<std::pair<std::variant<int8_t, RefTypeBuilder>, uint32_t>>&
      locals() const {
    return locals_;
  }
  const std::vector<std::variant<std::string, int>>& local_names() const {
    return local_names_;
  }
  std::optional<uint32_t> body_offset() const { return body_offset_; }
  void set_body_offset(uint32_t offset) { body_offset_ = offset; }

 private:
  WasmModuleBuilder* module_;
  std::string name_;
  uint32_t type_index_;
  uint32_t index_ = 0;
  std::vector<uint8_t> body_;
  std::vector<std::pair<std::variant<int8_t, RefTypeBuilder>, uint32_t>>
      locals_;
  std::vector<std::variant<std::string, int>> local_names_;
  std::optional<uint32_t> body_offset_;
};

// WasmGlobalBuilder
class WasmGlobalBuilder {
 public:
  WasmGlobalBuilder(WasmModuleBuilder* module,
                    std::variant<int8_t, RefTypeBuilder> type, bool mutable_val,
                    bool shared, std::vector<uint8_t> init);
  ~WasmGlobalBuilder() = default;
  WasmGlobalBuilder(const WasmGlobalBuilder&) = delete;
  WasmGlobalBuilder& operator=(const WasmGlobalBuilder&) = delete;

  WasmGlobalBuilder& ExportAs(const std::string& name);

  void set_index(uint32_t idx) { index_ = idx; }

  // Accessors
  WasmModuleBuilder* module() const { return module_; }
  const std::variant<int8_t, RefTypeBuilder>& type() const { return type_; }
  bool mutable_val() const { return mutable_val_; }
  bool shared() const { return shared_; }
  const std::vector<uint8_t>& init() const { return init_; }
  uint32_t index() const { return index_; }

 private:
  WasmModuleBuilder* module_;
  std::variant<int8_t, RefTypeBuilder> type_;
  bool mutable_val_;
  bool shared_;
  std::vector<uint8_t> init_;
  uint32_t index_ = 0;
};

// WasmTableBuilder
class WasmTableBuilder {
 public:
  WasmTableBuilder(WasmModuleBuilder* module,
                   std::variant<int8_t, RefTypeBuilder> type,
                   uint32_t initial_size, std::optional<uint32_t> max_size,
                   std::optional<std::vector<uint8_t>> init_expr,
                   bool is_shared, bool is_table64);
  ~WasmTableBuilder() = default;
  WasmTableBuilder(const WasmTableBuilder&) = delete;
  WasmTableBuilder& operator=(const WasmTableBuilder&) = delete;

  WasmTableBuilder& ExportAs(const std::string& name);

  void set_index(uint32_t idx) { index_ = idx; }

  // Accessors
  WasmModuleBuilder* module() const { return module_; }
  const std::variant<int8_t, RefTypeBuilder>& type() const { return type_; }
  uint32_t initial_size() const { return initial_size_; }
  bool has_max() const { return max_size_.has_value(); }
  uint32_t max_size() const {
    CHECK(has_max());
    return max_size_.value();
  }
  bool has_init() const { return init_expr_.has_value(); }
  const std::vector<uint8_t>& init_expr() const {
    CHECK(has_init());
    return init_expr_.value();
  }
  bool is_shared() const { return is_shared_; }
  bool is_table64() const { return is_table64_; }
  uint32_t index() const { return index_; }

 private:
  WasmModuleBuilder* module_;
  std::variant<int8_t, RefTypeBuilder> type_;
  uint32_t initial_size_;
  std::optional<uint32_t> max_size_;
  std::optional<std::vector<uint8_t>> init_expr_;
  bool is_shared_;
  bool is_table64_;
  uint32_t index_ = 0;
};

// ImportGroupBuilder
class ImportGroupBuilder {
 public:
  ImportGroupBuilder(WasmModuleBuilder* builder, uint8_t encoding,
                     std::string module,
                     std::optional<uint8_t> kind = std::nullopt,
                     std::optional<ImportEntry> prototype = std::nullopt);
  ~ImportGroupBuilder() = default;
  ImportGroupBuilder(const ImportGroupBuilder&) = delete;
  ImportGroupBuilder& operator=(const ImportGroupBuilder&) = delete;

  uint32_t AddFunction(const std::string& name, const WasmSig& type);
  uint32_t AddFunction(const std::string& name, uint32_t type_index);
  uint32_t AddGlobal(const std::string& name,
                     std::variant<int8_t, RefTypeBuilder> type,
                     bool mutable_val = false, bool shared = false);
  uint32_t AddMemory(const std::string& name, uint32_t initial = 0,
                     std::optional<uint32_t> maximum = std::nullopt,
                     bool shared = false, bool is_memory64 = false);
  uint32_t AddTable(const std::string& name, uint32_t initial = 0,
                    std::optional<uint32_t> maximum = std::nullopt,
                    std::variant<int8_t, RefTypeBuilder> type = kWasmFuncRef,
                    bool shared = false, bool is_table64 = false);
  uint32_t AddTag(const std::string& name, const WasmSig& type);
  uint32_t AddTag(const std::string& name, uint32_t type_index);

  // Accessors
  WasmModuleBuilder* builder() const { return builder_; }
  uint8_t encoding() const { return encoding_; }
  const std::string& module() const { return module_; }
  const std::vector<ImportEntry>& imports() const { return imports_; }

 private:
  WasmModuleBuilder* builder_;
  uint8_t encoding_;
  std::string module_;
  std::optional<uint8_t> kind_;
  std::optional<ImportEntry> prototype_;
  std::vector<ImportEntry> imports_;
};

// WasmModuleBuilder
class WasmModuleBuilder {
 public:
  WasmModuleBuilder();
  ~WasmModuleBuilder() = default;
  WasmModuleBuilder(const WasmModuleBuilder&) = delete;
  WasmModuleBuilder& operator=(const WasmModuleBuilder&) = delete;

  // Type management
  uint32_t AddType(const WasmSig& type, uint32_t supertype_idx = kNoSuperType,
                   bool is_final = true, bool is_shared = false);
  uint32_t AddStruct(const WasmStructType& struct_type);
  uint32_t AddStruct(std::vector<WasmField> fields,
                     std::optional<uint32_t> supertype = std::nullopt,
                     bool is_final = false, bool is_shared = false,
                     std::optional<uint32_t> descriptor = std::nullopt,
                     std::optional<uint32_t> describes = std::nullopt);
  uint32_t AddArray(std::variant<int8_t, RefTypeBuilder> type,
                    bool mutable_val = true, bool is_final = false,
                    bool is_shared = false,
                    std::optional<uint32_t> supertype = std::nullopt);
  uint32_t AddCont(const WasmSig& type);
  uint32_t AddCont(uint32_t type_index);
  uint32_t AddLiteralStringRef(const std::string& str);

  // Imports
  uint32_t AddImport(const std::string& module, const std::string& name,
                     const WasmSig& type, uint8_t kind = kExternalFunction);
  uint32_t AddImport(const std::string& module, const std::string& name,
                     uint32_t type_index, uint8_t kind);
  uint32_t AddImportedGlobal(const std::string& module,
                             const std::string& name,
                             std::variant<int8_t, RefTypeBuilder> type,
                             bool mutable_val = false, bool shared = false);
  uint32_t AddImportedMemory(const std::string& module,
                             const std::string& name, uint32_t initial = 0,
                             std::optional<uint32_t> maximum = std::nullopt,
                             bool shared = false, bool is_memory64 = false);
  uint32_t AddImportedTable(
      const std::string& module, const std::string& name, uint32_t initial = 0,
      std::optional<uint32_t> maximum = std::nullopt,
      std::variant<int8_t, RefTypeBuilder> type = kWasmFuncRef,
      bool shared = false, bool is_table64 = false);
  uint32_t AddImportedTag(const std::string& module, const std::string& name,
                          const WasmSig& type);
  uint32_t AddImportedTag(const std::string& module, const std::string& name,
                          uint32_t type_index);

  // Import groups
  ImportGroupBuilder* AddImportGroup(const std::string& module);
  ImportGroupBuilder* AddImportedFunctionGroup(const std::string& module,
                                               const WasmSig& type);
  ImportGroupBuilder* AddImportedFunctionGroup(const std::string& module,
                                                 uint32_t type_index);
  ImportGroupBuilder* AddImportedGlobalsGroup(
      const std::string& module, std::variant<int8_t, RefTypeBuilder> type,
      bool mutable_val = false, bool shared = false);
  ImportGroupBuilder* AddImportedTableGroup(
      const std::string& module, uint32_t initial = 0,
      std::optional<uint32_t> maximum = std::nullopt,
      std::variant<int8_t, RefTypeBuilder> type = kWasmFuncRef,
      bool shared = false, bool is_table64 = false);
  ImportGroupBuilder* AddImportedMemoryGroup(
      const std::string& module, uint32_t initial = 0,
      std::optional<uint32_t> maximum = std::nullopt, bool shared = false,
      bool is_memory64 = false);
  ImportGroupBuilder* AddImportedTagGroup(const std::string& module,
                                          const WasmSig& type);
  ImportGroupBuilder* AddImportedTagGroup(const std::string& module,
                                          uint32_t type_index);

  // Module entities
  uint32_t AddMemory(uint32_t min, std::optional<uint32_t> max = std::nullopt,
                     bool shared = false);
  uint32_t AddMemory64(uint32_t min,
                       std::optional<uint32_t> max = std::nullopt,
                       bool shared = false);
  WasmGlobalBuilder* AddGlobal(std::variant<int8_t, RefTypeBuilder> type,
                               bool mutable_val = false, bool shared = false,
                               std::optional<std::vector<uint8_t>> init =
                                   std::nullopt);
  WasmTableBuilder* AddTable(
      std::variant<int8_t, RefTypeBuilder> type, uint32_t initial_size,
      std::optional<uint32_t> max_size = std::nullopt,
      std::optional<std::vector<uint8_t>> init_expr = std::nullopt,
      bool shared = false, bool is_table64 = false);
  WasmTableBuilder* AddTable64(
      std::variant<int8_t, RefTypeBuilder> type, uint32_t initial_size,
      std::optional<uint32_t> max_size = std::nullopt,
      std::optional<std::vector<uint8_t>> init_expr = std::nullopt,
      bool shared = false);
  uint32_t AddTag(const WasmSig& type);
  uint32_t AddTag(uint32_t type_index);
  WasmFunctionBuilder* AddFunction(
      const std::string& name, const WasmSig& type,
      const std::vector<std::string>& arg_names = {});
  WasmFunctionBuilder* AddFunction(const std::string& name,
                                   uint32_t type_index,
                                   const std::vector<std::string>& arg_names =
                                       {});

  // Exports
  WasmModuleBuilder& AddExport(const std::string& name, uint32_t index);
  WasmModuleBuilder& AddExportOfKind(const std::string& name, uint8_t kind,
                                     uint32_t index);
  WasmModuleBuilder& ExportMemoryAs(
      const std::string& name,
      std::optional<uint32_t> memory_index = std::nullopt);

  // Start
  WasmModuleBuilder& AddStart(uint32_t start_index);

  // Data segments
  uint32_t AddActiveDataSegment(uint32_t memory_index,
                                std::span<const uint8_t> offset,
                                std::span<const uint8_t> data,
                                bool is_shared = false);
  uint32_t AddPassiveDataSegment(std::span<const uint8_t> data,
                                 bool is_shared = false);

  // Element segments
  uint32_t AddActiveElementSegment(
      uint32_t table, std::span<const uint8_t> offset,
      const std::vector<uint32_t>& elements,
      std::optional<std::variant<int8_t, RefTypeBuilder>> type = std::nullopt,
      bool is_shared = false);
  uint32_t AddActiveElementSegment(
      uint32_t table, std::span<const uint8_t> offset,
      const std::vector<std::vector<uint8_t>>& elements,
      std::variant<int8_t, RefTypeBuilder> type, bool is_shared = false);
  uint32_t AddPassiveElementSegment(
      const std::vector<uint32_t>& elements,
      std::optional<std::variant<int8_t, RefTypeBuilder>> type = std::nullopt,
      bool is_shared = false);
  uint32_t AddPassiveElementSegment(
      const std::vector<std::vector<uint8_t>>& elements,
      std::variant<int8_t, RefTypeBuilder> type, bool is_shared = false);
  uint32_t AddDeclarativeElementSegment(
      const std::vector<uint32_t>& elements,
      std::optional<std::variant<int8_t, RefTypeBuilder>> type = std::nullopt,
      bool is_shared = false);
  uint32_t AddDeclarativeElementSegment(
      const std::vector<std::vector<uint8_t>>& elements,
      std::variant<int8_t, RefTypeBuilder> type, bool is_shared = false);

  // Recursive groups
  WasmModuleBuilder& StartRecGroup();
  WasmModuleBuilder& EndRecGroup();

  // Metadata
  WasmModuleBuilder& SetName(const std::string& name);
  WasmModuleBuilder& SetCompilationPriority(
      uint32_t function_index, uint32_t compilation_priority,
      std::optional<uint32_t> optimization_priority = std::nullopt);
  WasmModuleBuilder& SetInstructionFrequencies(
      uint32_t function_index,
      const std::vector<InstructionFrequency>& frequencies);
  WasmModuleBuilder& SetCallTargets(
      uint32_t function_index,
      const std::vector<CallTargetAtOffset>& call_targets);

  // Explicit/custom sections
  WasmModuleBuilder& AddExplicitSection(std::span<const uint8_t> bytes);
  WasmModuleBuilder& AddCustomSection(const std::string& name,
                                      std::span<const uint8_t> bytes);
  std::vector<uint8_t> CreateCustomSection(const std::string& name,
                                           std::span<const uint8_t> bytes);

  // Serialization
  std::vector<uint8_t> ToBuffer(bool debug = false) const;
  std::vector<uint8_t> ToArray(bool debug = false) const;

  // Validation
  static void CheckExpr(std::span<const uint8_t> expr);
  static std::vector<uint8_t> DefaultFor(
      std::variant<int8_t, RefTypeBuilder> type);

  // Accessors
  const std::vector<std::variant<WasmSig, WasmStructType, WasmArrayType,
                                 WasmContType>>& types() const {
    return types_;
  }
  const std::vector<std::variant<ImportEntry,
                                 std::unique_ptr<ImportGroupBuilder>>>&
  imports() const {
    return imports_;
  }
  const std::vector<ExportEntry>& exports() const { return exports_; }
  const std::vector<std::unique_ptr<WasmGlobalBuilder>>& globals() const {
    return globals_;
  }
  const std::vector<std::unique_ptr<WasmTableBuilder>>& tables() const {
    return tables_;
  }
  const std::vector<std::unique_ptr<WasmFunctionBuilder>>& functions() const {
    return functions_;
  }
  const std::vector<uint32_t>& tags() const { return tags_; }
  const std::vector<DataSegment>& data_segments() const {
    return data_segments_;
  }
  const std::vector<WasmElemSegment>& element_segments() const {
    return element_segments_;
  }
  const std::vector<std::string>& stringrefs() const { return stringrefs_; }
  const std::vector<std::vector<uint8_t>>& explicit_sections() const {
    return explicit_;
  }
  const std::vector<std::pair<uint32_t, uint32_t>>& rec_groups() const {
    return rec_groups_;
  }
  const std::string& name() const { return name_; }
  std::optional<uint32_t> start_index() const { return start_index_; }

  // Internal
  uint32_t AllocateImportIndex(uint8_t kind);

 private:
  // Emission helpers
  void EmitExternType(Binary& bin, const ImportEntry& imp) const;
  void EmitTypeSection(Binary& bin, bool debug) const;
  void EmitImportSection(Binary& bin, bool debug) const;
  void EmitFunctionSection(Binary& bin, bool debug) const;
  void EmitTableSection(Binary& bin, bool debug) const;
  void EmitMemorySection(Binary& bin, bool debug) const;
  void EmitTagSection(Binary& bin, bool debug) const;
  void EmitStringRefSection(Binary& bin, bool debug) const;
  void EmitGlobalSection(Binary& bin, bool debug) const;
  void EmitExportSection(Binary& bin, bool debug) const;
  void EmitStartSection(Binary& bin, bool debug) const;
  void EmitElementSection(Binary& bin, bool debug) const;
  void EmitDataCountSection(Binary& bin, bool debug) const;
  void EmitCodeSection(Binary& bin, bool debug) const;
  void EmitDataSection(Binary& bin, bool debug) const;
  void EmitExplicitSections(Binary& bin, bool debug) const;
  void EmitNameSection(Binary& bin, bool debug) const;
  void EmitCompilationPrioritySection(Binary& bin, bool debug) const;
  void EmitInstructionFrequencySection(Binary& bin, bool debug) const;
  void EmitCallTargetSection(Binary& bin, bool debug) const;

  // State
  std::vector<std::variant<WasmSig, WasmStructType, WasmArrayType, WasmContType>>
      types_;
  std::vector<
      std::variant<ImportEntry, std::unique_ptr<ImportGroupBuilder>>>
      imports_;
  std::vector<ExportEntry> exports_;
  std::vector<std::string> stringrefs_;
  std::vector<std::unique_ptr<WasmGlobalBuilder>> globals_;
  std::vector<std::unique_ptr<WasmTableBuilder>> tables_;
  std::vector<uint32_t> tags_;
  std::vector<std::pair<uint32_t, uint32_t>> memories_;  // (min, max_or_0)
  std::vector<bool> memory_is_shared_;
  std::vector<bool> memory_is_memory64_;
  std::vector<std::optional<uint32_t>> memory_max_;
  std::vector<std::unique_ptr<WasmFunctionBuilder>> functions_;
  std::vector<WasmElemSegment> element_segments_;
  std::vector<DataSegment> data_segments_;
  std::vector<std::vector<uint8_t>> explicit_;
  std::vector<std::pair<uint32_t, uint32_t>> rec_groups_;  // (start, size)
  std::vector<int> rec_group_stack_;  // nesting counter
  std::string name_;
  std::optional<uint32_t> start_index_;

  // Import counters
  uint32_t num_imported_funcs_ = 0;
  uint32_t num_imported_globals_ = 0;
  uint32_t num_imported_tables_ = 0;
  uint32_t num_imported_tags_ = 0;
  uint32_t num_imported_memories_ = 0;

  // Metadata
  std::vector<std::pair<uint32_t, CompilationPriority>> compilation_priorities_;
  std::vector<std::pair<uint32_t, std::vector<InstructionFrequency>>>
      instruction_frequencies_;
  std::vector<std::pair<uint32_t, std::vector<CallTargetAtOffset>>>
      call_targets_;
};

WasmSig MakeSig(std::vector<std::variant<int8_t, RefTypeBuilder>> params,
                std::vector<std::variant<int8_t, RefTypeBuilder>> results);

std::vector<uint8_t> WasmI32Const(int32_t val);
std::vector<uint8_t> WasmI64Const(int64_t val);
std::vector<uint8_t> WasmF32Const(float f);
std::vector<uint8_t> WasmF64Const(double d);
std::vector<uint8_t> WasmS128Const(std::span<const uint8_t> bytes);
std::vector<uint8_t> WasmSignedLeb(int32_t val, int max_len = kMaxVarInt32Size);
std::vector<uint8_t> WasmSignedLeb64(int64_t val,
                                     int max_len = kMaxVarInt64Size);
std::vector<uint8_t> WasmUnsignedLeb(uint32_t val,
                                     int max_len = kMaxVarInt32Size);
std::vector<uint8_t> WasmEncodeHeapType(const RefTypeBuilder& type);

const char* GetTrapMessage(int trap_code);

extern const WasmSig kSig_i_i;
extern const WasmSig kSig_l_l;
extern const WasmSig kSig_i_l;
extern const WasmSig kSig_i_ii;
extern const WasmSig kSig_i_iii;
extern const WasmSig kSig_i_iiii;
extern const WasmSig kSig_v_iiii;
extern const WasmSig kSig_l_iiii;
extern const WasmSig kSig_l_i;
extern const WasmSig kSig_f_i;
extern const WasmSig kSig_i_f;
extern const WasmSig kSig_i_ff;
extern const WasmSig kSig_f_ff;
extern const WasmSig kSig_f_ffff;
extern const WasmSig kSig_f_lff;
extern const WasmSig kSig_d_dd;
extern const WasmSig kSig_d_dddd;
extern const WasmSig kSig_l_ll;
extern const WasmSig kSig_l_llll;
extern const WasmSig kSig_i_dd;
extern const WasmSig kSig_v_v;
extern const WasmSig kSig_i_v;
extern const WasmSig kSig_l_v;
extern const WasmSig kSig_f_v;
extern const WasmSig kSig_d_v;
extern const WasmSig kSig_v_i;
extern const WasmSig kSig_v_ii;
extern const WasmSig kSig_v_iii;
extern const WasmSig kSig_v_l;
extern const WasmSig kSig_v_li;
extern const WasmSig kSig_v_lii;
extern const WasmSig kSig_v_d;
extern const WasmSig kSig_v_dd;
extern const WasmSig kSig_v_ddi;
extern const WasmSig kSig_ii_v;
extern const WasmSig kSig_iii_v;
extern const WasmSig kSig_ii_i;
extern const WasmSig kSig_iii_i;
extern const WasmSig kSig_ii_ii;
extern const WasmSig kSig_iii_ii;
extern const WasmSig kSig_v_f;
extern const WasmSig kSig_f_f;
extern const WasmSig kSig_f_d;
extern const WasmSig kSig_d_d;
extern const WasmSig kSig_d_f;
extern const WasmSig kSig_d_i;
extern const WasmSig kSig_r_r;
extern const WasmSig kSig_a_a;
extern const WasmSig kSig_i_r;
extern const WasmSig kSig_v_r;
extern const WasmSig kSig_v_a;
extern const WasmSig kSig_v_rr;
extern const WasmSig kSig_v_aa;
extern const WasmSig kSig_r_v;
extern const WasmSig kSig_a_v;
extern const WasmSig kSig_a_i;
extern const WasmSig kSig_s_i;
extern const WasmSig kSig_i_s;

}  // namespace wasm_builder
}  // namespace cat


#endif  // BUILDER_WASM_BUILDER_H_

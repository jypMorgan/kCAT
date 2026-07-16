#include "WasmBuilder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {

constexpr size_t kMaxTypes = 200'000;
constexpr size_t kMaxFunctions = 200'000;
constexpr size_t kMaxGlobals = 100'000;
constexpr size_t kMaxTables = 10'000;
constexpr size_t kMaxMemories = 1'000;
constexpr size_t kMaxTags = 50'000;
constexpr size_t kMaxDataSegments = 200'000;
constexpr size_t kMaxElementSegments = 200'000;
constexpr size_t kMaxExports = 200'000;
constexpr size_t kMaxImports = 200'000;
constexpr size_t kMaxStringRefs = 100'000;
constexpr size_t kMaxRecGroups = 100'000;
constexpr size_t kMaxExplicitSections = 10'000;
constexpr size_t kMaxModuleBytes = 512 * 1024 * 1024;  // 512 MiB
constexpr size_t kMaxExprBytes = 64 * 1024 * 1024;     // 64 MiB per expr
constexpr size_t kMaxLocalsPerFunction = 1'000'000;
constexpr size_t kMaxCompilationPriorities = 200'000;
constexpr size_t kMaxInstructionFrequencies = 200'000;
constexpr size_t kMaxCallTargets = 200'000;

thread_local int g_stack_depth = 0;
constexpr int kMaxStackDepth = 1024;

struct StackDepthGuard {
  StackDepthGuard() {
    ++g_stack_depth;
    CHECK(g_stack_depth < kMaxStackDepth);
  }
  ~StackDepthGuard() { --g_stack_depth; }
};

bool IsValidUTF8(std::string_view str) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(str.data());
  size_t len = str.size();
  for (size_t i = 0; i < len;) {
    uint8_t c = bytes[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= len) return false;
      if ((bytes[i + 1] & 0xC0) != 0x80) return false;
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= len) return false;
      if ((bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80)
        return false;
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= len) return false;
      if ((bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80 ||
          (bytes[i + 3] & 0xC0) != 0x80)
        return false;
      i += 4;
    } else {
      return false;
    }
  }
  return true;
}

bool IsValidValueType(int8_t type) {
  using namespace cat::wasm_builder;
  switch (static_cast<uint8_t>(type)) {
    case static_cast<uint8_t>(kWasmI32):
    case static_cast<uint8_t>(kWasmI64):
    case static_cast<uint8_t>(kWasmF32):
    case static_cast<uint8_t>(kWasmF64):
    case static_cast<uint8_t>(kWasmS128):
    case static_cast<uint8_t>(kWasmI8):
    case static_cast<uint8_t>(kWasmI16):
    case static_cast<uint8_t>(kWasmF16):
      return true;
    default:
      return false;
  }
}

bool IsValidReferenceType(int8_t type) {
  using namespace cat::wasm_builder;
  switch (static_cast<uint8_t>(type)) {
    case static_cast<uint8_t>(kWasmFuncRef):
    case static_cast<uint8_t>(kWasmExternRef):
    case static_cast<uint8_t>(kWasmAnyRef):
    case static_cast<uint8_t>(kWasmEqRef):
    case static_cast<uint8_t>(kWasmStructRef):
    case static_cast<uint8_t>(kWasmArrayRef):
    case static_cast<uint8_t>(kWasmNullFuncRef):
    case static_cast<uint8_t>(kWasmNullExternRef):
    case static_cast<uint8_t>(kWasmNullRef):
    case static_cast<uint8_t>(kWasmExnRef):
    case static_cast<uint8_t>(kWasmNullExnRef):
    case static_cast<uint8_t>(kWasmStringRef):
    case static_cast<uint8_t>(kWasmI31Ref):
    case static_cast<uint8_t>(kWasmStringViewWtf8):
    case static_cast<uint8_t>(kWasmStringViewWtf16):
    case static_cast<uint8_t>(kWasmStringViewIter):
    case static_cast<uint8_t>(kWasmContRef):
    case static_cast<uint8_t>(kWasmNullContRef):
    case static_cast<uint8_t>(kWasmWaitqueueRef):
    case static_cast<uint8_t>(kWasmNullWaitqueueRef):
      return true;
    default:
      return false;
  }
}

bool IsValidTableType(int8_t type) {
  return IsValidReferenceType(type);
}

void ValidateTypeVariant(const std::variant<int8_t, cat::wasm_builder::RefTypeBuilder>& type) {
  std::visit(
      [](auto&& t) {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, int8_t>) {
          CHECK(IsValidValueType(t) || IsValidReferenceType(t));
        }
      },
      type);
}

}  // anonymous namespace

namespace cat {
namespace wasm_builder {

// Trap messages
const char* kTrapMsgs[] = {
    "unreachable",
    "memory access out of bounds",
    "divide by zero",
    "divide result unrepresentable",
    "remainder by zero",
    "float unrepresentable in integer range",
    "table index is out of bounds",
    "null function",
    "function signature mismatch",
    "operation does not support unaligned accesses",
    "data segment out of bounds",
    "element segment out of bounds",
    "rethrowing null value",
    "requested new array is too large",
    "array element access out of bounds",
    "dereferencing a null pointer",
    "illegal cast",
};

const int kTrapCount = sizeof(kTrapMsgs) / sizeof(kTrapMsgs[0]);

const char* GetTrapMessage(int trap_code) {
  CHECK(trap_code >= 0 && trap_code < kTrapCount);
  return kTrapMsgs[trap_code];
}

#define SIG(name, ...) const WasmSig kSig_##name = MakeSig(__VA_ARGS__)

SIG(i_i,  {kWasmI32}, {kWasmI32});
SIG(l_l,  {kWasmI64}, {kWasmI64});
SIG(i_l,  {kWasmI64}, {kWasmI32});
SIG(i_ii, {kWasmI32, kWasmI32}, {kWasmI32});
SIG(i_iii, {kWasmI32, kWasmI32, kWasmI32}, {kWasmI32});
SIG(i_iiii, {kWasmI32, kWasmI32, kWasmI32, kWasmI32}, {kWasmI32});
SIG(v_iiii, {kWasmI32, kWasmI32, kWasmI32, kWasmI32}, {});
SIG(l_iiii, {kWasmI32, kWasmI32, kWasmI32, kWasmI32}, {kWasmI64});
SIG(l_i,  {kWasmI32}, {kWasmI64});
SIG(f_i,  {kWasmI32}, {kWasmF32});
SIG(i_f,  {kWasmF32}, {kWasmI32});
SIG(i_ff, {kWasmF32, kWasmF32}, {kWasmI32});
SIG(f_ff, {kWasmF32, kWasmF32}, {kWasmF32});
SIG(f_ffff, {kWasmF32, kWasmF32, kWasmF32, kWasmF32}, {kWasmF32});
SIG(f_lff, {kWasmI64, kWasmF32, kWasmF32}, {kWasmF32});
SIG(d_dd, {kWasmF64, kWasmF64}, {kWasmF64});
SIG(d_dddd, {kWasmF64, kWasmF64, kWasmF64, kWasmF64}, {kWasmF64});
SIG(l_ll, {kWasmI64, kWasmI64}, {kWasmI64});
SIG(l_llll, {kWasmI64, kWasmI64, kWasmI64, kWasmI64}, {kWasmI64});
SIG(i_dd, {kWasmF64, kWasmF64}, {kWasmI32});
SIG(v_v,  {}, {});
SIG(i_v,  {}, {kWasmI32});
SIG(l_v,  {}, {kWasmI64});
SIG(f_v,  {}, {kWasmF32});
SIG(d_v,  {}, {kWasmF64});
SIG(v_i,  {kWasmI32}, {});
SIG(v_ii, {kWasmI32, kWasmI32}, {});
SIG(v_iii, {kWasmI32, kWasmI32, kWasmI32}, {});
SIG(v_l,  {kWasmI64}, {});
SIG(v_li, {kWasmI64, kWasmI32}, {});
SIG(v_lii, {kWasmI64, kWasmI32, kWasmI32}, {});
SIG(v_d,  {kWasmF64}, {});
SIG(v_dd, {kWasmF64, kWasmF64}, {});
SIG(v_ddi, {kWasmF64, kWasmF64, kWasmI32}, {});
SIG(ii_v, {}, {kWasmI32, kWasmI32});
SIG(iii_v, {}, {kWasmI32, kWasmI32, kWasmI32});
SIG(ii_i, {kWasmI32}, {kWasmI32, kWasmI32});
SIG(iii_i, {kWasmI32}, {kWasmI32, kWasmI32, kWasmI32});
SIG(ii_ii, {kWasmI32, kWasmI32}, {kWasmI32, kWasmI32});
SIG(iii_ii, {kWasmI32, kWasmI32}, {kWasmI32, kWasmI32, kWasmI32});
SIG(v_f,  {kWasmF32}, {});
SIG(f_f,  {kWasmF32}, {kWasmF32});
SIG(f_d,  {kWasmF64}, {kWasmF32});
SIG(d_d,  {kWasmF64}, {kWasmF64});
SIG(d_f,  {kWasmF32}, {kWasmF64});
SIG(d_i,  {kWasmI32}, {kWasmF64});
SIG(r_r,  {kWasmExternRef}, {kWasmExternRef});
SIG(a_a,  {kWasmFuncRef}, {kWasmFuncRef});
SIG(i_r,  {kWasmExternRef}, {kWasmI32});
SIG(v_r,  {kWasmExternRef}, {});
SIG(v_a,  {kWasmFuncRef}, {});
SIG(v_rr, {kWasmExternRef, kWasmExternRef}, {});
SIG(v_aa, {kWasmFuncRef, kWasmFuncRef}, {});
SIG(r_v,  {}, {kWasmExternRef});
SIG(a_v,  {}, {kWasmFuncRef});
SIG(a_i,  {kWasmI32}, {kWasmFuncRef});
SIG(s_i,  {kWasmI32}, {kWasmS128});
SIG(i_s,  {kWasmS128}, {kWasmI32});

#undef SIG

WasmSig MakeSig(std::vector<std::variant<int8_t, RefTypeBuilder>> params,
                std::vector<std::variant<int8_t, RefTypeBuilder>> results) {
  WasmSig sig;
  sig.params = std::move(params);
  sig.results = std::move(results);
  return sig;
}

Binary::Binary() {
  buffer_.reserve(8192);
}

void Binary::Reset() {
  buffer_.clear();
}

void Binary::EnsureSpace(size_t needed) {
  CHECK(needed <= std::numeric_limits<size_t>::max() / 4);
  if (buffer_.capacity() - buffer_.size() >= needed) return;

  size_t current = buffer_.size();
  size_t new_cap = buffer_.capacity();
  if (new_cap == 0) new_cap = 8192;
  while (new_cap - current < needed) {
    CHECK(new_cap <= std::numeric_limits<size_t>::max() / 2);
    new_cap *= 2;
  }
  if (new_cap < 8192) new_cap = 8192;
  buffer_.reserve(new_cap);
}

void Binary::EmitU8(uint8_t val) {
  EnsureSpace(1);
  buffer_.push_back(val);
}

void Binary::EmitU16(uint16_t val) {
  EnsureSpace(2);
  buffer_.push_back(val & 0xFF);
  buffer_.push_back((val >> 8) & 0xFF);
}

void Binary::EmitU32(uint32_t val) {
  EnsureSpace(4);
  buffer_.push_back(val & 0xFF);
  buffer_.push_back((val >> 8) & 0xFF);
  buffer_.push_back((val >> 16) & 0xFF);
  buffer_.push_back((val >> 24) & 0xFF);
}

void Binary::EmitU32v(uint32_t val) {
  EnsureSpace(kMaxVarInt32Size);
  for (int i = 0; i < kMaxVarInt32Size; ++i) {
    uint8_t b = val & 0x7f;
    val >>= 7;
    if (val == 0) {
      buffer_.push_back(b);
      return;
    }
    buffer_.push_back(b | 0x80);
  }
  CHECK(false);
}

void Binary::EmitU64v(uint64_t val) {
  EnsureSpace(kMaxVarInt64Size);
  for (int i = 0; i < kMaxVarInt64Size; ++i) {
    uint8_t b = val & 0x7f;
    val >>= 7;
    if (val == 0) {
      buffer_.push_back(b);
      return;
    }
    buffer_.push_back(b | 0x80);
  }
  CHECK(false);
}

void Binary::EmitS32v(int32_t val) {
  EnsureSpace(kMaxVarInt32Size);
  for (int i = 0; i < kMaxVarInt32Size; ++i) {
    uint8_t b = val & 0x7f;
    val >>= 7;
    bool done = (val == 0 && (b & 0x40) == 0) ||
                (val == -1 && (b & 0x40) != 0);
    if (done) {
      buffer_.push_back(b);
      return;
    }
    buffer_.push_back(b | 0x80);
  }
  CHECK(false);
}

void Binary::EmitS64v(int64_t val) {
  EnsureSpace(kMaxVarInt64Size);
  for (int i = 0; i < kMaxVarInt64Size; ++i) {
    uint8_t b = val & 0x7f;
    val >>= 7;
    bool done = (val == 0 && (b & 0x40) == 0) ||
                (val == -1 && (b & 0x40) != 0);
    if (done) {
      buffer_.push_back(b);
      return;
    }
    buffer_.push_back(b | 0x80);
  }
  CHECK(false);
}

void Binary::EmitBytes(std::span<const uint8_t> data) {
  EnsureSpace(data.size());
  buffer_.insert(buffer_.end(), data.begin(), data.end());
}

void Binary::EmitString(std::string_view str) {
  CHECK(str.size() <= std::numeric_limits<uint32_t>::max());
  CHECK(IsValidUTF8(str));
  EmitU32v(static_cast<uint32_t>(str.size()));
  EnsureSpace(str.size());
  buffer_.insert(buffer_.end(),
                  reinterpret_cast<const uint8_t*>(str.data()),
                  reinterpret_cast<const uint8_t*>(str.data()) + str.size());
}

void Binary::EmitType(std::variant<int8_t, RefTypeBuilder> type) {
  if (std::holds_alternative<int8_t>(type)) {
    int8_t t = std::get<int8_t>(type);
    EmitU8(static_cast<uint8_t>(t) & kLeb128Mask);
  } else {
    const RefTypeBuilder& rt = std::get<RefTypeBuilder>(type);
    EmitU8(rt.opcode());
    if (rt.is_shared()) EmitU8(kWasmSharedTypeForm);
    if (rt.is_exact()) EmitU8(kWasmExact);
    EmitHeapType(rt.heap_type());
  }
}

void Binary::EmitHeapType(int32_t heap_type) {
  EmitS32v(heap_type);
}

void Binary::EmitInitExpr(std::span<const uint8_t> expr) {
  CHECK(expr.size() <= kMaxExprBytes);
  EmitBytes(expr);
  EmitU8(kExprEnd);
}

void Binary::EmitHeader() {
  static const uint8_t kHeader[8] = {kWasmH0, kWasmH1, kWasmH2, kWasmH3,
                                     kWasmV0, kWasmV1, kWasmV2, kWasmV3};
  EmitBytes(std::span<const uint8_t>(kHeader, 8));
}

void Binary::EmitSection(uint8_t section_code,
                         const std::function<void(Binary&)>& generator) {
  EmitU8(section_code);
  Binary section;
  generator(section);
  size_t section_len = section.length();
  CHECK(section_len <= std::numeric_limits<uint32_t>::max());
  EmitU32v(static_cast<uint32_t>(section_len));
  EmitBytes(std::span<const uint8_t>(section.buffer()));
}

std::vector<uint8_t> Binary::TruncBuffer() const {
  return buffer_;
}

WasmFunctionBuilder::WasmFunctionBuilder(
    WasmModuleBuilder* module, std::string name, uint32_t type_index,
    std::vector<std::variant<std::string, int>> arg_names)
    : module_(module),
      name_(std::move(name)),
      type_index_(type_index),
      local_names_(std::move(arg_names)) {
  CHECK(module != nullptr);
}

WasmFunctionBuilder& WasmFunctionBuilder::ExportAs(const std::string& name) {
  CHECK(module_ != nullptr);
  module_->AddExportOfKind(name, kExternalFunction, index_);
  return *this;
}

WasmFunctionBuilder& WasmFunctionBuilder::ExportFunc() {
  return ExportAs(name_);
}

WasmFunctionBuilder& WasmFunctionBuilder::AddBody(std::span<const uint8_t> body) {
  WasmModuleBuilder::CheckExpr(body);
  body_.assign(body.begin(), body.end());
  body_.push_back(kExprEnd);
  return *this;
}

WasmFunctionBuilder& WasmFunctionBuilder::AddBodyWithEnd(
    std::span<const uint8_t> body) {
  WasmModuleBuilder::CheckExpr(body);
  CHECK(!body.empty() && body.back() == kExprEnd);
  body_.assign(body.begin(), body.end());
  return *this;
}

WasmFunctionBuilder& WasmFunctionBuilder::AddLocals(
    std::variant<int8_t, RefTypeBuilder> type, uint32_t count,
    const std::vector<std::string>& names) {
  uint32_t current_total = 0;
  for (const auto& decl : locals_) {
    current_total += decl.second;
  }
  CHECK(current_total <= kMaxLocalsPerFunction);
  CHECK(count <= kMaxLocalsPerFunction - current_total);
  CHECK(names.size() <= static_cast<size_t>(count));
  ValidateTypeVariant(type);
  locals_.push_back({type, count});
  for (const auto& n : names) {
    local_names_.push_back(n);
  }
  if (count > names.size()) {
    local_names_.push_back(static_cast<int>(count - names.size()));
  }
  return *this;
}

uint32_t WasmFunctionBuilder::NumLocalNames() const {
  uint32_t count = 0;
  for (const auto& ln : local_names_) {
    if (std::holds_alternative<std::string>(ln)) ++count;
  }
  return count;
}

WasmGlobalBuilder::WasmGlobalBuilder(
    WasmModuleBuilder* module, std::variant<int8_t, RefTypeBuilder> type,
    bool mutable_val, bool shared, std::vector<uint8_t> init)
    : module_(module),
      type_(std::move(type)),
      mutable_val_(mutable_val),
      shared_(shared),
      init_(std::move(init)) {
  CHECK(module != nullptr);
  ValidateTypeVariant(type_);
}

WasmGlobalBuilder& WasmGlobalBuilder::ExportAs(const std::string& name) {
  CHECK(module_ != nullptr);
  module_->AddExportOfKind(name, kExternalGlobal, index_);
  return *this;
}

WasmTableBuilder::WasmTableBuilder(
    WasmModuleBuilder* module, std::variant<int8_t, RefTypeBuilder> type,
    uint32_t initial_size, std::optional<uint32_t> max_size,
    std::optional<std::vector<uint8_t>> init_expr, bool is_shared,
    bool is_table64)
    : module_(module),
      type_(std::move(type)),
      initial_size_(initial_size),
      max_size_(max_size),
      init_expr_(std::move(init_expr)),
      is_shared_(is_shared),
      is_table64_(is_table64) {
  CHECK(module != nullptr);
  ValidateTypeVariant(type_);
  bool is_ref = false;
  if (std::holds_alternative<int8_t>(type_)) {
    is_ref = IsValidTableType(std::get<int8_t>(type_));
  } else {
    is_ref = true;
  }
  CHECK(is_ref);
  if (init_expr_.has_value()) {
    WasmModuleBuilder::CheckExpr(init_expr_.value());
  }
}

WasmTableBuilder& WasmTableBuilder::ExportAs(const std::string& name) {
  CHECK(module_ != nullptr);
  module_->AddExportOfKind(name, kExternalTable, index_);
  return *this;
}

ImportGroupBuilder::ImportGroupBuilder(
    WasmModuleBuilder* builder, uint8_t encoding, std::string module,
    std::optional<uint8_t> kind, std::optional<ImportEntry> prototype)
    : builder_(builder),
      encoding_(encoding),
      module_(std::move(module)),
      kind_(kind),
      prototype_(prototype) {
  CHECK(builder_ != nullptr);
  CHECK(encoding == kCompactImportByModule ||
        encoding == kCompactImportByModuleAndType);
  if (kind.has_value()) {
    CHECK(kind.value() <= kExternalTag);
  }
}

uint32_t ImportGroupBuilder::AddFunction(const std::string& name,
                                         const WasmSig& type) {
  uint32_t type_index = builder_->AddType(type);
  return AddFunction(name, type_index);
}

uint32_t ImportGroupBuilder::AddFunction(const std::string& name,
                                           uint32_t type_index) {
  CHECK(!kind_.has_value());
  CHECK(builder_->functions().empty());
  ImportEntry entry;
  entry.name = name;
  entry.kind = kExternalFunction;
  entry.encoding = encoding_;
  entry.type_index = type_index;
  imports_.push_back(entry);
  return builder_->AllocateImportIndex(kExternalFunction);
}

uint32_t ImportGroupBuilder::AddGlobal(
    const std::string& name, std::variant<int8_t, RefTypeBuilder> type,
    bool mutable_val, bool shared) {
  CHECK(!kind_.has_value());
  CHECK(builder_->globals().empty());
  ValidateTypeVariant(type);
  ImportEntry entry;
  entry.name = name;
  entry.kind = kExternalGlobal;
  entry.encoding = encoding_;
  entry.type = std::move(type);
  entry.mutable_ = mutable_val;
  entry.shared = shared;
  imports_.push_back(entry);
  return builder_->AllocateImportIndex(kExternalGlobal);
}

uint32_t ImportGroupBuilder::AddMemory(const std::string& name,
                                         uint32_t initial,
                                         std::optional<uint32_t> maximum,
                                         bool shared, bool is_memory64) {
  CHECK(!kind_.has_value());
  CHECK(builder_->functions().empty() && builder_->globals().empty() &&
        builder_->tables().empty() && builder_->tags().empty());
  ImportEntry entry;
  entry.name = name;
  entry.kind = kExternalMemory;
  entry.encoding = encoding_;
  entry.initial = initial;
  entry.maximum = maximum;
  entry.shared = shared;
  entry.is_memory64 = is_memory64;
  imports_.push_back(entry);
  return builder_->AllocateImportIndex(kExternalMemory);
}

uint32_t ImportGroupBuilder::AddTable(
    const std::string& name, uint32_t initial,
    std::optional<uint32_t> maximum,
    std::variant<int8_t, RefTypeBuilder> type, bool shared, bool is_table64) {
  CHECK(!kind_.has_value());
  CHECK(builder_->tables().empty());
  ValidateTypeVariant(type);
  bool is_ref = false;
  if (std::holds_alternative<int8_t>(type)) {
    is_ref = IsValidTableType(std::get<int8_t>(type));
  } else {
    is_ref = true;
  }
  CHECK(is_ref);
  ImportEntry entry;
  entry.name = name;
  entry.kind = kExternalTable;
  entry.encoding = encoding_;
  entry.initial = initial;
  entry.maximum = maximum;
  entry.type = std::move(type);
  entry.shared = shared;
  entry.is_table64 = is_table64;
  imports_.push_back(entry);
  return builder_->AllocateImportIndex(kExternalTable);
}

uint32_t ImportGroupBuilder::AddTag(const std::string& name,
                                    const WasmSig& type) {
  uint32_t type_index = builder_->AddType(type);
  return AddTag(name, type_index);
}

uint32_t ImportGroupBuilder::AddTag(const std::string& name,
                                    uint32_t type_index) {
  CHECK(!kind_.has_value());
  CHECK(builder_->tags().empty());
  ImportEntry entry;
  entry.name = name;
  entry.kind = kExternalTag;
  entry.encoding = encoding_;
  entry.type_index = type_index;
  imports_.push_back(entry);
  return builder_->AllocateImportIndex(kExternalTag);
}

WasmModuleBuilder::WasmModuleBuilder() = default;

uint32_t WasmModuleBuilder::AddType(const WasmSig& type,
                                     uint32_t supertype_idx, bool is_final,
                                     bool is_shared) {
  CHECK(types_.size() < kMaxTypes);
  WasmSig copy = type;
  copy.is_final = is_final;
  copy.is_shared = is_shared;
  copy.supertype = supertype_idx;

  for (const auto& p : copy.params) {
    ValidateTypeVariant(p);
  }
  for (const auto& r : copy.results) {
    ValidateTypeVariant(r);
  }

  if (supertype_idx != kNoSuperType) {
    CHECK(supertype_idx < types_.size());
  }

  types_.push_back(copy);
  return static_cast<uint32_t>(types_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddStruct(const WasmStructType& struct_type) {
  CHECK(types_.size() < kMaxTypes);
  WasmStructType copy = struct_type;
  if (copy.supertype != kNoSuperType) {
    CHECK(copy.supertype < types_.size());
  }
  if (copy.descriptor.has_value()) {
    CHECK(copy.descriptor.value() < types_.size());
  }
  if (copy.describes.has_value()) {
    CHECK(copy.describes.value() < types_.size());
  }
  for (const auto& f : copy.fields) {
    ValidateTypeVariant(f.type);
  }
  types_.push_back(copy);
  return static_cast<uint32_t>(types_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddStruct(std::vector<WasmField> fields,
                                       std::optional<uint32_t> supertype,
                                       bool is_final, bool is_shared,
                                       std::optional<uint32_t> descriptor,
                                       std::optional<uint32_t> describes) {
  CHECK(types_.size() < kMaxTypes);
  WasmStructType st;
  st.fields = std::move(fields);
  st.is_final = is_final;
  st.is_shared = is_shared;
  st.supertype = supertype.value_or(kNoSuperType);
  st.descriptor = descriptor;
  st.describes = describes;

  if (st.supertype != kNoSuperType) {
    CHECK(st.supertype < types_.size());
  }
  if (st.descriptor.has_value()) {
    CHECK(st.descriptor.value() < types_.size());
  }
  if (st.describes.has_value()) {
    CHECK(st.describes.value() < types_.size());
  }
  for (const auto& f : st.fields) {
    ValidateTypeVariant(f.type);
  }
  types_.push_back(st);
  return static_cast<uint32_t>(types_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddArray(
    std::variant<int8_t, RefTypeBuilder> type, bool mutable_val,
    bool is_final, bool is_shared, std::optional<uint32_t> supertype) {
  CHECK(types_.size() < kMaxTypes);
  ValidateTypeVariant(type);

  WasmArrayType at;
  at.type = std::move(type);
  at.mutability = mutable_val;
  at.is_final = is_final;
  at.is_shared = is_shared;
  at.supertype = supertype.value_or(kNoSuperType);
  if (at.supertype != kNoSuperType) {
    CHECK(at.supertype < types_.size());
  }
  types_.push_back(at);
  return static_cast<uint32_t>(types_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddCont(const WasmSig& type) {
  uint32_t idx = AddType(type);
  return AddCont(idx);
}

uint32_t WasmModuleBuilder::AddCont(uint32_t type_index) {
  CHECK(type_index < types_.size());
  WasmContType ct;
  ct.type_index = type_index;
  types_.push_back(ct);
  return static_cast<uint32_t>(types_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddLiteralStringRef(const std::string& str) {
  CHECK(stringrefs_.size() < kMaxStringRefs);
  CHECK(str.size() <= std::numeric_limits<uint32_t>::max());
  stringrefs_.push_back(str);
  return static_cast<uint32_t>(stringrefs_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddImport(const std::string& module,
                                       const std::string& name,
                                       const WasmSig& type, uint8_t kind) {
  CHECK(!module.empty() || !name.empty());
  CHECK(kind == kExternalFunction);
  uint32_t type_index = AddType(type);
  return AddImport(module, name, type_index, kind);
}

uint32_t WasmModuleBuilder::AddImport(const std::string& module,
                                       const std::string& name,
                                       uint32_t type_index, uint8_t kind) {
  CHECK(kind == kExternalFunction);
  CHECK(type_index < types_.size());
  CHECK(functions_.empty());
  CHECK(imports_.size() < kMaxImports);

  ImportEntry imp;
  imp.module = module;
  imp.name = name;
  imp.kind = kind;
  imp.type_index = type_index;
  imports_.push_back(imp);
  return num_imported_funcs_++;
}

uint32_t WasmModuleBuilder::AddImportedGlobal(
    const std::string& module, const std::string& name,
    std::variant<int8_t, RefTypeBuilder> type, bool mutable_val,
    bool shared) {
  CHECK(globals_.empty());
  CHECK(imports_.size() < kMaxImports);
  ValidateTypeVariant(type);

  ImportEntry imp;
  imp.module = module;
  imp.name = name;
  imp.kind = kExternalGlobal;
  imp.type = std::move(type);
  imp.mutable_ = mutable_val;
  imp.shared = shared;
  imports_.push_back(imp);
  return num_imported_globals_++;
}

uint32_t WasmModuleBuilder::AddImportedMemory(
    const std::string& module, const std::string& name, uint32_t initial,
    std::optional<uint32_t> maximum, bool shared, bool is_memory64) {
  CHECK(memories_.empty());
  CHECK(imports_.size() < kMaxImports);

  ImportEntry imp;
  imp.module = module;
  imp.name = name;
  imp.kind = kExternalMemory;
  imp.initial = initial;
  imp.maximum = maximum;
  imp.shared = shared;
  imp.is_memory64 = is_memory64;
  imports_.push_back(imp);
  return num_imported_memories_++;
}

uint32_t WasmModuleBuilder::AddImportedTable(
    const std::string& module, const std::string& name, uint32_t initial,
    std::optional<uint32_t> maximum,
    std::variant<int8_t, RefTypeBuilder> type, bool shared, bool is_table64) {
  CHECK(tables_.empty());
  CHECK(imports_.size() < kMaxImports);
  ValidateTypeVariant(type);

  bool is_ref = false;
  if (std::holds_alternative<int8_t>(type)) {
    is_ref = IsValidTableType(std::get<int8_t>(type));
  } else {
    is_ref = true;
  }
  CHECK(is_ref);

  ImportEntry imp;
  imp.module = module;
  imp.name = name;
  imp.kind = kExternalTable;
  imp.initial = initial;
  imp.maximum = maximum;
  imp.type = std::move(type);
  imp.shared = shared;
  imp.is_table64 = is_table64;
  imports_.push_back(imp);
  return num_imported_tables_++;
}

uint32_t WasmModuleBuilder::AddImportedTag(const std::string& module,
                                              const std::string& name,
                                              const WasmSig& type) {
  uint32_t type_index = AddType(type);
  return AddImportedTag(module, name, type_index);
}

uint32_t WasmModuleBuilder::AddImportedTag(const std::string& module,
                                              const std::string& name,
                                              uint32_t type_index) {
  CHECK(tags_.empty());
  CHECK(type_index < types_.size());
  CHECK(imports_.size() < kMaxImports);

  ImportEntry imp;
  imp.module = module;
  imp.name = name;
  imp.kind = kExternalTag;
  imp.type_index = type_index;
  imports_.push_back(imp);
  return num_imported_tags_++;
}

ImportGroupBuilder* WasmModuleBuilder::AddImportGroup(
    const std::string& module) {
  CHECK(imports_.size() < kMaxImports);
  auto group = std::make_unique<ImportGroupBuilder>(
      this, kCompactImportByModule, module);
  ImportGroupBuilder* ptr = group.get();
  imports_.push_back(std::move(group));
  return ptr;
}

ImportGroupBuilder* WasmModuleBuilder::AddImportedFunctionGroup(
    const std::string& module, const WasmSig& type) {
  uint32_t type_index = AddType(type);
  return AddImportedFunctionGroup(module, type_index);
}

ImportGroupBuilder* WasmModuleBuilder::AddImportedFunctionGroup(
    const std::string& module, uint32_t type_index) {
  CHECK(type_index < types_.size());
  CHECK(imports_.size() < kMaxImports);
  ImportEntry proto;
  proto.kind = kExternalFunction;
  proto.type_index = type_index;
  auto group = std::make_unique<ImportGroupBuilder>(
      this, kCompactImportByModuleAndType, module, kExternalFunction, proto);
  ImportGroupBuilder* ptr = group.get();
  imports_.push_back(std::move(group));
  return ptr;
}

ImportGroupBuilder* WasmModuleBuilder::AddImportedGlobalsGroup(
    const std::string& module, std::variant<int8_t, RefTypeBuilder> type,
    bool mutable_val, bool shared) {
  CHECK(imports_.size() < kMaxImports);
  ValidateTypeVariant(type);
  ImportEntry proto;
  proto.kind = kExternalGlobal;
  proto.type = std::move(type);
  proto.mutable_ = mutable_val;
  proto.shared = shared;
  auto group = std::make_unique<ImportGroupBuilder>(
      this, kCompactImportByModuleAndType, module, kExternalGlobal, proto);
  ImportGroupBuilder* ptr = group.get();
  imports_.push_back(std::move(group));
  return ptr;
}

ImportGroupBuilder* WasmModuleBuilder::AddImportedTableGroup(
    const std::string& module, uint32_t initial,
    std::optional<uint32_t> maximum,
    std::variant<int8_t, RefTypeBuilder> type, bool shared, bool is_table64) {
  CHECK(imports_.size() < kMaxImports);
  ValidateTypeVariant(type);
  ImportEntry proto;
  proto.kind = kExternalTable;
  proto.initial = initial;
  proto.maximum = maximum;
  proto.type = std::move(type);
  proto.shared = shared;
  proto.is_table64 = is_table64;
  auto group = std::make_unique<ImportGroupBuilder>(
      this, kCompactImportByModuleAndType, module, kExternalTable, proto);
  ImportGroupBuilder* ptr = group.get();
  imports_.push_back(std::move(group));
  return ptr;
}

ImportGroupBuilder* WasmModuleBuilder::AddImportedMemoryGroup(
    const std::string& module, uint32_t initial,
    std::optional<uint32_t> maximum, bool shared, bool is_memory64) {
  CHECK(imports_.size() < kMaxImports);
  ImportEntry proto;
  proto.kind = kExternalMemory;
  proto.initial = initial;
  proto.maximum = maximum;
  proto.shared = shared;
  proto.is_memory64 = is_memory64;
  auto group = std::make_unique<ImportGroupBuilder>(
      this, kCompactImportByModuleAndType, module, kExternalMemory, proto);
  ImportGroupBuilder* ptr = group.get();
  imports_.push_back(std::move(group));
  return ptr;
}

ImportGroupBuilder* WasmModuleBuilder::AddImportedTagGroup(
    const std::string& module, const WasmSig& type) {
  uint32_t type_index = AddType(type);
  return AddImportedTagGroup(module, type_index);
}

ImportGroupBuilder* WasmModuleBuilder::AddImportedTagGroup(
    const std::string& module, uint32_t type_index) {
  CHECK(type_index < types_.size());
  CHECK(imports_.size() < kMaxImports);
  ImportEntry proto;
  proto.kind = kExternalTag;
  proto.type_index = type_index;
  auto group = std::make_unique<ImportGroupBuilder>(
      this, kCompactImportByModuleAndType, module, kExternalTag, proto);
  ImportGroupBuilder* ptr = group.get();
  imports_.push_back(std::move(group));
  return ptr;
}

uint32_t WasmModuleBuilder::AddMemory(uint32_t min,
                                       std::optional<uint32_t> max,
                                       bool shared) {
  CHECK(memories_.size() < kMaxMemories);
  uint32_t index =
      num_imported_memories_ + static_cast<uint32_t>(memories_.size());
  memories_.push_back({min, max.value_or(0)});
  memory_is_shared_.push_back(shared);
  memory_is_memory64_.push_back(false);
  memory_max_.push_back(max);
  return index;
}

uint32_t WasmModuleBuilder::AddMemory64(uint32_t min,
                                           std::optional<uint32_t> max,
                                           bool shared) {
  CHECK(memories_.size() < kMaxMemories);
  uint32_t index =
      num_imported_memories_ + static_cast<uint32_t>(memories_.size());
  memories_.push_back({min, max.value_or(0)});
  memory_is_shared_.push_back(shared);
  memory_is_memory64_.push_back(true);
  memory_max_.push_back(max);
  return index;
}

WasmGlobalBuilder* WasmModuleBuilder::AddGlobal(
    std::variant<int8_t, RefTypeBuilder> type, bool mutable_val, bool shared,
    std::optional<std::vector<uint8_t>> init) {
  CHECK(globals_.size() < kMaxGlobals);
  ValidateTypeVariant(type);
  if (!init.has_value()) {
    init = DefaultFor(type);
  }
  CheckExpr(init.value());
  auto glob = std::make_unique<WasmGlobalBuilder>(
      this, type, mutable_val, shared, std::move(init.value()));
  glob->set_index(static_cast<uint32_t>(globals_.size()) +
                  num_imported_globals_);
  WasmGlobalBuilder* ptr = glob.get();
  globals_.push_back(std::move(glob));
  return ptr;
}

WasmTableBuilder* WasmModuleBuilder::AddTable(
    std::variant<int8_t, RefTypeBuilder> type, uint32_t initial_size,
    std::optional<uint32_t> max_size,
    std::optional<std::vector<uint8_t>> init_expr, bool shared,
    bool is_table64) {
  CHECK(tables_.size() < kMaxTables);
  ValidateTypeVariant(type);

  bool is_ref = false;
  if (std::holds_alternative<int8_t>(type)) {
    is_ref = IsValidTableType(std::get<int8_t>(type));
  } else {
    is_ref = true;
  }
  CHECK(is_ref);

  if (init_expr.has_value()) {
    CheckExpr(init_expr.value());
  }
  auto table = std::make_unique<WasmTableBuilder>(
      this, type, initial_size, max_size, std::move(init_expr), shared,
      is_table64);
  table->set_index(static_cast<uint32_t>(tables_.size()) +
                   num_imported_tables_);
  WasmTableBuilder* ptr = table.get();
  tables_.push_back(std::move(table));
  return ptr;
}

WasmTableBuilder* WasmModuleBuilder::AddTable64(
    std::variant<int8_t, RefTypeBuilder> type, uint32_t initial_size,
    std::optional<uint32_t> max_size,
    std::optional<std::vector<uint8_t>> init_expr, bool shared) {
  return AddTable(type, initial_size, max_size, init_expr, shared, true);
}

uint32_t WasmModuleBuilder::AddTag(const WasmSig& type) {
  uint32_t type_index = AddType(type);
  return AddTag(type_index);
}

uint32_t WasmModuleBuilder::AddTag(uint32_t type_index) {
  CHECK(type_index < types_.size());
  CHECK(tags_.size() < kMaxTags);
  uint32_t tag_index =
      static_cast<uint32_t>(tags_.size()) + num_imported_tags_;
  tags_.push_back(type_index);
  return tag_index;
}

WasmFunctionBuilder* WasmModuleBuilder::AddFunction(
    const std::string& name, const WasmSig& type,
    const std::vector<std::string>& arg_names) {
  uint32_t type_index = AddType(type);
  return AddFunction(name, type_index, arg_names);
}

WasmFunctionBuilder* WasmModuleBuilder::AddFunction(
    const std::string& name, uint32_t type_index,
    const std::vector<std::string>& arg_names) {
  CHECK(type_index < types_.size());
  CHECK(functions_.size() < kMaxFunctions);

  const WasmSig* sig = nullptr;
  std::visit(
      [&](auto&& t) {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, WasmSig>) {
          sig = &t;
        }
      },
      types_[type_index]);
  CHECK(sig != nullptr);

  uint32_t num_args = static_cast<uint32_t>(sig->params.size());
  std::vector<std::variant<std::string, int>> names;
  if (arg_names.size() > num_args) {
    CHECK(false);
  }
  for (const auto& n : arg_names) {
    names.push_back(n);
  }
  if (num_args > arg_names.size()) {
    names.push_back(static_cast<int>(num_args - arg_names.size()));
  }

  auto func = std::make_unique<WasmFunctionBuilder>(
      this, name, type_index, std::move(names));
  func->set_index(static_cast<uint32_t>(functions_.size()) +
                  num_imported_funcs_);
  WasmFunctionBuilder* ptr = func.get();
  functions_.push_back(std::move(func));
  return ptr;
}

WasmModuleBuilder& WasmModuleBuilder::AddExport(const std::string& name,
                                                  uint32_t index) {
  CHECK(!name.empty());
  CHECK(exports_.size() < kMaxExports);
  exports_.push_back({name, kExternalFunction, index});
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::AddExportOfKind(const std::string& name,
                                                       uint8_t kind,
                                                       uint32_t index) {
  CHECK(!name.empty());
  CHECK(kind <= kExternalExactFunction);
  CHECK(exports_.size() < kMaxExports);
  if (index == std::numeric_limits<uint32_t>::max() &&
      kind != kExternalTable && kind != kExternalMemory) {
    CHECK(false);
  }
  exports_.push_back({name, kind, index});
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::ExportMemoryAs(
    const std::string& name, std::optional<uint32_t> memory_index) {
  uint32_t idx = memory_index.value_or(0);
  if (!memory_index.has_value()) {
    uint32_t total =
        static_cast<uint32_t>(memories_.size()) + num_imported_memories_;
    CHECK(total == 1);
  }
  CHECK(idx < memories_.size() + num_imported_memories_);
  CHECK(exports_.size() < kMaxExports);
  exports_.push_back({name, kExternalMemory, idx});
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::AddStart(uint32_t start_index) {
  uint32_t total_funcs =
      static_cast<uint32_t>(functions_.size()) + num_imported_funcs_;
  CHECK(start_index < total_funcs);
  start_index_ = start_index;
  return *this;
}

uint32_t WasmModuleBuilder::AddActiveDataSegment(
    uint32_t memory_index, std::span<const uint8_t> offset,
    std::span<const uint8_t> data, bool is_shared) {
  CheckExpr(offset);
  CHECK(data_segments_.size() < kMaxDataSegments);
  DataSegment seg;
  seg.is_active = true;
  seg.is_shared = is_shared;
  seg.mem_index = memory_index;
  seg.offset.assign(offset.begin(), offset.end());
  seg.data.assign(data.begin(), data.end());
  data_segments_.push_back(seg);
  return static_cast<uint32_t>(data_segments_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddPassiveDataSegment(std::span<const uint8_t> data,
                                                   bool is_shared) {
  CHECK(data_segments_.size() < kMaxDataSegments);
  DataSegment seg;
  seg.is_active = false;
  seg.is_shared = is_shared;
  seg.data.assign(data.begin(), data.end());
  data_segments_.push_back(seg);
  return static_cast<uint32_t>(data_segments_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddActiveElementSegment(
    uint32_t table, std::span<const uint8_t> offset,
    const std::vector<uint32_t>& elements,
    std::optional<std::variant<int8_t, RefTypeBuilder>> type, bool is_shared) {
  CheckExpr(offset);
  CHECK(element_segments_.size() < kMaxElementSegments);
  WasmElemSegment seg;
  seg.table = table;
  seg.offset.assign(offset.begin(), offset.end());
  seg.type = type;
  for (auto e : elements) {
    seg.elements.push_back(e);
  }
  seg.is_shared = is_shared;
  element_segments_.push_back(seg);
  return static_cast<uint32_t>(element_segments_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddActiveElementSegment(
    uint32_t table, std::span<const uint8_t> offset,
    const std::vector<std::vector<uint8_t>>& elements,
    std::variant<int8_t, RefTypeBuilder> type, bool is_shared) {
  CheckExpr(offset);
  for (const auto& e : elements) CheckExpr(e);
  CHECK(element_segments_.size() < kMaxElementSegments);
  WasmElemSegment seg;
  seg.table = table;
  seg.offset.assign(offset.begin(), offset.end());
  seg.type = type;
  for (const auto& e : elements) {
    seg.elements.push_back(e);
  }
  seg.is_shared = is_shared;
  element_segments_.push_back(seg);
  return static_cast<uint32_t>(element_segments_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddPassiveElementSegment(
    const std::vector<uint32_t>& elements,
    std::optional<std::variant<int8_t, RefTypeBuilder>> type, bool is_shared) {
  CHECK(element_segments_.size() < kMaxElementSegments);
  WasmElemSegment seg;
  seg.type = type;
  for (auto e : elements) {
    seg.elements.push_back(e);
  }
  seg.is_shared = is_shared;
  element_segments_.push_back(seg);
  return static_cast<uint32_t>(element_segments_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddPassiveElementSegment(
    const std::vector<std::vector<uint8_t>>& elements,
    std::variant<int8_t, RefTypeBuilder> type, bool is_shared) {
  for (const auto& e : elements) CheckExpr(e);
  CHECK(element_segments_.size() < kMaxElementSegments);
  WasmElemSegment seg;
  seg.type = type;
  for (const auto& e : elements) {
    seg.elements.push_back(e);
  }
  seg.is_shared = is_shared;
  element_segments_.push_back(seg);
  return static_cast<uint32_t>(element_segments_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddDeclarativeElementSegment(
    const std::vector<uint32_t>& elements,
    std::optional<std::variant<int8_t, RefTypeBuilder>> type, bool is_shared) {
  CHECK(element_segments_.size() < kMaxElementSegments);
  WasmElemSegment seg;
  seg.is_decl = true;
  seg.type = type;
  for (auto e : elements) {
    seg.elements.push_back(e);
  }
  seg.is_shared = is_shared;
  element_segments_.push_back(seg);
  return static_cast<uint32_t>(element_segments_.size()) - 1;
}

uint32_t WasmModuleBuilder::AddDeclarativeElementSegment(
    const std::vector<std::vector<uint8_t>>& elements,
    std::variant<int8_t, RefTypeBuilder> type, bool is_shared) {
  for (const auto& e : elements) CheckExpr(e);
  CHECK(element_segments_.size() < kMaxElementSegments);
  WasmElemSegment seg;
  seg.is_decl = true;
  seg.type = type;
  for (const auto& e : elements) {
    seg.elements.push_back(e);
  }
  seg.is_shared = is_shared;
  element_segments_.push_back(seg);
  return static_cast<uint32_t>(element_segments_.size()) - 1;
}

WasmModuleBuilder& WasmModuleBuilder::StartRecGroup() {
  CHECK(rec_groups_.size() < kMaxRecGroups);
  rec_group_stack_.push_back(1);
  rec_groups_.push_back(
      {static_cast<uint32_t>(types_.size()), 0});
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::EndRecGroup() {
  CHECK(!rec_group_stack_.empty());
  rec_group_stack_.pop_back();
  auto& last = rec_groups_.back();
  CHECK(last.second == 0);
  last.second = static_cast<uint32_t>(types_.size()) - last.first;
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::SetName(const std::string& name) {
  name_ = name;
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::SetCompilationPriority(
    uint32_t function_index, uint32_t compilation_priority,
    std::optional<uint32_t> optimization_priority) {
  uint32_t total_funcs =
      static_cast<uint32_t>(functions_.size()) + num_imported_funcs_;
  CHECK(function_index < total_funcs);
  CHECK(compilation_priorities_.size() < kMaxCompilationPriorities);
  compilation_priorities_.push_back(
      {function_index, {compilation_priority, optimization_priority}});
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::SetInstructionFrequencies(
    uint32_t function_index,
    const std::vector<InstructionFrequency>& frequencies) {
  uint32_t total_funcs =
      static_cast<uint32_t>(functions_.size()) + num_imported_funcs_;
  CHECK(function_index < total_funcs);
  CHECK(frequencies.size() <= kMaxInstructionFrequencies);
  instruction_frequencies_.push_back({function_index, frequencies});
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::SetCallTargets(
    uint32_t function_index,
    const std::vector<CallTargetAtOffset>& call_targets) {
  uint32_t total_funcs =
      static_cast<uint32_t>(functions_.size()) + num_imported_funcs_;
  CHECK(function_index < total_funcs);
  CHECK(call_targets.size() <= kMaxCallTargets);
  call_targets_.push_back({function_index, call_targets});
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::AddExplicitSection(
    std::span<const uint8_t> bytes) {
  CHECK(explicit_.size() < kMaxExplicitSections);
  explicit_.emplace_back(bytes.begin(), bytes.end());
  return *this;
}

WasmModuleBuilder& WasmModuleBuilder::AddCustomSection(
    const std::string& name, std::span<const uint8_t> bytes) {
  CHECK(explicit_.size() < kMaxExplicitSections);
  explicit_.push_back(CreateCustomSection(name, bytes));
  return *this;
}

std::vector<uint8_t> WasmModuleBuilder::CreateCustomSection(
    const std::string& name, std::span<const uint8_t> bytes) {
  Binary bin;
  bin.EmitU8(0);
  Binary content;
  content.EmitString(name);
  content.EmitBytes(bytes);
  bin.EmitU32v(static_cast<uint32_t>(content.length()));
  bin.EmitBytes(std::span<const uint8_t>(content.buffer()));
  return bin.TruncBuffer();
}

std::vector<uint8_t> WasmModuleBuilder::DefaultFor(
    std::variant<int8_t, RefTypeBuilder> type) {
  if (std::holds_alternative<int8_t>(type)) {
    int8_t t = std::get<int8_t>(type);
    switch (t) {
      case kWasmI32:
        return WasmI32Const(0);
      case kWasmI64:
        return WasmI64Const(0);
      case kWasmF32:
        return WasmF32Const(0.0f);
      case kWasmF64:
        return WasmF64Const(0.0);
      case kWasmS128:
        return {kSimdPrefix, 0x0c, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0};
      case kWasmI8:
      case kWasmI16:
      case kWasmF16:
        CHECK(false);  // Packed types have no standalone default initializer
        break;
      default:
        break;
    }
  }
  int32_t heap_type;
  if (std::holds_alternative<RefTypeBuilder>(type)) {
    heap_type = std::get<RefTypeBuilder>(type).heap_type();
  } else {
    heap_type = std::get<int8_t>(type);
  }
  auto leb = WasmSignedLeb(heap_type, kMaxVarInt32Size);
  std::vector<uint8_t> result = {kExprRefNull};
  result.insert(result.end(), leb.begin(), leb.end());
  return result;
}

void WasmModuleBuilder::CheckExpr(std::span<const uint8_t> expr) {
  CHECK(expr.size() <= kMaxExprBytes);
}

uint32_t WasmModuleBuilder::AllocateImportIndex(uint8_t kind) {
  switch (kind) {
    case kExternalFunction:
      return num_imported_funcs_++;
    case kExternalGlobal:
      return num_imported_globals_++;
    case kExternalMemory:
      return num_imported_memories_++;
    case kExternalTable:
      return num_imported_tables_++;
    case kExternalTag:
      return num_imported_tags_++;
    default:
      CHECK(false);
      return 0;
  }
}

void WasmModuleBuilder::EmitExternType(Binary& bin,
                                        const ImportEntry& imp) const {
  bin.EmitU8(imp.kind);
  if (imp.kind == kExternalFunction || imp.kind == kExternalExactFunction) {
    bin.EmitU32v(imp.type_index);
  } else if (imp.kind == kExternalGlobal) {
    bin.EmitType(imp.type);
    uint8_t flags = (imp.mutable_ ? 1 : 0) | (imp.shared ? 0b10 : 0);
    bin.EmitU8(flags);
  } else if (imp.kind == kExternalMemory) {
    bool has_max = imp.maximum.has_value();
    uint8_t limits_byte =
        (imp.is_memory64 ? 4 : 0) | (imp.shared ? 2 : 0) | (has_max ? 1 : 0);
    bin.EmitU8(limits_byte);
    if (imp.is_memory64) {
      bin.EmitU64v(imp.initial);
      if (has_max) bin.EmitU64v(imp.maximum.value());
    } else {
      bin.EmitU32v(imp.initial);
      if (has_max) bin.EmitU32v(imp.maximum.value());
    }
  } else if (imp.kind == kExternalTable) {
    bin.EmitType(imp.type);
    bool has_max = imp.maximum.has_value();
    uint8_t limits_byte =
        (imp.is_table64 ? 4 : 0) | (imp.shared ? 2 : 0) | (has_max ? 1 : 0);
    bin.EmitU8(limits_byte);
    bin.EmitU32v(imp.initial);
    if (has_max) bin.EmitU32v(imp.maximum.value());
  } else if (imp.kind == kExternalTag) {
    bin.EmitU32v(kExceptionAttribute);
    bin.EmitU32v(imp.type_index);
  } else {
    CHECK(false);
  }
}

void WasmModuleBuilder::EmitTypeSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kTypeSectionCode, [&](Binary& section) {
    uint32_t length_with_groups =
        static_cast<uint32_t>(types_.size());
    for (const auto& group : rec_groups_) {
      CHECK(group.second >= 1);
      uint32_t decrement = group.second - 1;
      CHECK(decrement <= length_with_groups);
      length_with_groups -= decrement;
    }
    section.EmitU32v(length_with_groups);

    size_t rec_group_index = 0;
    for (size_t i = 0; i < types_.size(); ++i) {
      if (rec_group_index < rec_groups_.size() &&
          rec_groups_[rec_group_index].first == i) {
        section.EmitU8(kWasmRecursiveTypeGroupForm);
        section.EmitU32v(rec_groups_[rec_group_index].second);
        ++rec_group_index;
      }

      std::visit(
          [&](auto&& type) {
            using T = std::decay_t<decltype(type)>;
            if constexpr (std::is_same_v<T, WasmSig>) {
              if (type.supertype != kNoSuperType) {
                section.EmitU8(type.is_final ? kWasmSubtypeFinalForm
                                              : kWasmSubtypeForm);
                section.EmitU8(1);
                section.EmitU32v(type.supertype);
              } else if (!type.is_final) {
                section.EmitU8(kWasmSubtypeForm);
                section.EmitU8(0);
              }
              if (type.is_shared) section.EmitU8(kWasmSharedTypeForm);
              section.EmitU8(kWasmFunctionTypeForm);
              section.EmitU32v(
                  static_cast<uint32_t>(type.params.size()));
              for (const auto& p : type.params) section.EmitType(p);
              section.EmitU32v(
                  static_cast<uint32_t>(type.results.size()));
              for (const auto& r : type.results) section.EmitType(r);
            } else if constexpr (std::is_same_v<T, WasmStructType>) {
              if (type.supertype != kNoSuperType) {
                section.EmitU8(type.is_final ? kWasmSubtypeFinalForm
                                              : kWasmSubtypeForm);
                section.EmitU8(1);
                section.EmitU32v(type.supertype);
              } else if (!type.is_final) {
                section.EmitU8(kWasmSubtypeForm);
                section.EmitU8(0);
              }
              if (type.is_shared) section.EmitU8(kWasmSharedTypeForm);
              if (type.describes.has_value()) {
                section.EmitU8(kWasmDescribesTypeForm);
                section.EmitU32v(type.describes.value());
              }
              if (type.descriptor.has_value()) {
                section.EmitU8(kWasmDescriptorTypeForm);
                section.EmitU32v(type.descriptor.value());
              }
              section.EmitU8(kWasmStructTypeForm);
              section.EmitU32v(
                  static_cast<uint32_t>(type.fields.size()));
              for (const auto& field : type.fields) {
                section.EmitType(field.type);
                section.EmitU8(field.mutability ? 1 : 0);
              }
            } else if constexpr (std::is_same_v<T, WasmArrayType>) {
              if (type.supertype != kNoSuperType) {
                section.EmitU8(type.is_final ? kWasmSubtypeFinalForm
                                              : kWasmSubtypeForm);
                section.EmitU8(1);
                section.EmitU32v(type.supertype);
              } else if (!type.is_final) {
                section.EmitU8(kWasmSubtypeForm);
                section.EmitU8(0);
              }
              if (type.is_shared) section.EmitU8(kWasmSharedTypeForm);
              section.EmitU8(kWasmArrayTypeForm);
              section.EmitType(type.type);
              section.EmitU8(type.mutability ? 1 : 0);
            } else if constexpr (std::is_same_v<T, WasmContType>) {
              section.EmitU8(kWasmContTypeForm);
              section.EmitU32v(type.type_index);
            }
          },
          types_[i]);
    }
  });
}

void WasmModuleBuilder::EmitImportSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kImportSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(imports_.size()));
    for (const auto& imp_var : imports_) {
      std::visit(
          [&](auto&& imp) {
            using T = std::decay_t<decltype(imp)>;
            if constexpr (std::is_same_v<T, ImportEntry>) {
              section.EmitString(imp.module);
              section.EmitString(imp.name);
              EmitExternType(section, imp);
            } else if constexpr (std::is_same_v<
                                     T, std::unique_ptr<ImportGroupBuilder>>) {
              const auto& group = *imp;
              section.EmitString(group.module());
              if (group.encoding() == kCompactImportByModule) {
                section.EmitString("");
                section.EmitU8(kCompactImportByModule);
                section.EmitU32v(
                    static_cast<uint32_t>(group.imports().size()));
                for (const auto& sub : group.imports()) {
                  section.EmitString(sub.name);
                  EmitExternType(section, sub);
                }
              } else if (group.encoding() ==
                         kCompactImportByModuleAndType) {
                section.EmitString("");
                section.EmitU8(kCompactImportByModuleAndType);
                if (!group.imports().empty()) {
                  EmitExternType(section, group.imports()[0]);
                }
                section.EmitU32v(
                    static_cast<uint32_t>(group.imports().size()));
                for (const auto& sub : group.imports()) {
                  section.EmitString(sub.name);
                }
              }
            }
          },
          imp_var);
    }
  });
}

void WasmModuleBuilder::EmitFunctionSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kFunctionSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(functions_.size()));
    for (const auto& func : functions_) {
      CHECK(func->type_index() < types_.size());
      section.EmitU32v(func->type_index());
    }
  });
}

void WasmModuleBuilder::EmitTableSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kTableSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(tables_.size()));
    for (const auto& table : tables_) {
      if (table->has_init()) {
        section.EmitU8(0x40);
        section.EmitU8(0x00);
      }
      section.EmitType(table->type());
      uint8_t limits_byte = (table->is_table64() ? 4 : 0) |
                            (table->is_shared() ? 2 : 0) |
                            (table->has_max() ? 1 : 0);
      section.EmitU8(limits_byte);
      if (table->is_table64()) {
        section.EmitU64v(table->initial_size());
        if (table->has_max()) section.EmitU64v(table->max_size());
      } else {
        section.EmitU32v(table->initial_size());
        if (table->has_max()) section.EmitU32v(table->max_size());
      }
      if (table->has_init()) {
        section.EmitInitExpr(table->init_expr());
      }
    }
  });
}

void WasmModuleBuilder::EmitMemorySection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kMemorySectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(memories_.size()));
    for (size_t i = 0; i < memories_.size(); ++i) {
      bool has_max = memory_max_[i].has_value();
      bool is_shared = memory_is_shared_[i];
      bool is_memory64 = memory_is_memory64_[i];
      uint8_t limits_byte =
          (is_memory64 ? 4 : 0) | (is_shared ? 2 : 0) | (has_max ? 1 : 0);
      section.EmitU8(limits_byte);
      if (is_memory64) {
        section.EmitU64v(memories_[i].first);
        if (has_max) section.EmitU64v(memory_max_[i].value());
      } else {
        section.EmitU32v(memories_[i].first);
        if (has_max) section.EmitU32v(memory_max_[i].value());
      }
    }
  });
}

void WasmModuleBuilder::EmitTagSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kTagSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(tags_.size()));
    for (uint32_t type_index : tags_) {
      CHECK(type_index < types_.size());
      section.EmitU32v(kExceptionAttribute);
      section.EmitU32v(type_index);
    }
  });
}

void WasmModuleBuilder::EmitStringRefSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kStringRefSectionCode, [&](Binary& section) {
    section.EmitU32v(0);
    section.EmitU32v(static_cast<uint32_t>(stringrefs_.size()));
    for (const auto& str : stringrefs_) {
      section.EmitString(str);
    }
  });
}

void WasmModuleBuilder::EmitGlobalSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kGlobalSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(globals_.size()));
    for (const auto& global : globals_) {
      section.EmitType(global->type());
      uint8_t flags =
          (global->mutable_val() ? 1 : 0) | (global->shared() ? 0b10 : 0);
      section.EmitU8(flags);
      section.EmitInitExpr(global->init());
    }
  });
}

void WasmModuleBuilder::EmitExportSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kExportSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(exports_.size()));
    for (const auto& exp : exports_) {
      section.EmitString(exp.name);
      section.EmitU8(exp.kind);
      section.EmitU32v(exp.index);
    }
  });
}

void WasmModuleBuilder::EmitStartSection(Binary& bin, bool /*debug*/) const {
  CHECK(start_index_.has_value());
  bin.EmitSection(kStartSectionCode, [&](Binary& section) {
    section.EmitU32v(start_index_.value());
  });
}

void WasmModuleBuilder::EmitElementSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kElementSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(element_segments_.size()));
    for (const auto& seg : element_segments_) {
      uint8_t shared_flag = seg.is_shared ? 0b1000 : 0;
      if (seg.IsActive()) {
        CHECK(seg.table.has_value());
        if (seg.table.value() == 0 && !seg.type.has_value()) {
          if (seg.ExpressionsAsElements()) {
            section.EmitU8(0x04 | shared_flag);
            section.EmitInitExpr(seg.offset);
          } else {
            section.EmitU8(0x00 | shared_flag);
            section.EmitInitExpr(seg.offset);
          }
        } else {
          if (seg.ExpressionsAsElements()) {
            section.EmitU8(0x06 | shared_flag);
            section.EmitU32v(seg.table.value());
            section.EmitInitExpr(seg.offset);
            section.EmitType(seg.type.value());
          } else {
            section.EmitU8(0x02 | shared_flag);
            section.EmitU32v(seg.table.value());
            section.EmitInitExpr(seg.offset);
            section.EmitU8(kExternalFunction);
          }
        }
      } else {
        if (seg.ExpressionsAsElements()) {
          if (seg.IsPassive()) {
            section.EmitU8(0x05 | shared_flag);
          } else {
            section.EmitU8(0x07 | shared_flag);
          }
          section.EmitType(seg.type.value());
        } else {
          if (seg.IsPassive()) {
            section.EmitU8(0x01 | shared_flag);
          } else {
            section.EmitU8(0x03 | shared_flag);
          }
          section.EmitU8(kExternalFunction);
        }
      }

      section.EmitU32v(
          static_cast<uint32_t>(seg.elements.size()));
      for (const auto& elem : seg.elements) {
        std::visit(
            [&](auto&& e) {
              using T = std::decay_t<decltype(e)>;
              if constexpr (std::is_same_v<T, uint32_t>) {
                section.EmitU32v(e);
              } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                section.EmitInitExpr(e);
              }
            },
            elem);
      }
    }
  });
}

void WasmModuleBuilder::EmitDataCountSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kDataCountSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(data_segments_.size()));
  });
}

void WasmModuleBuilder::EmitCodeSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kCodeSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(functions_.size()));
    for (const auto& func : functions_) {
      if (func->locals().empty()) {
        size_t body_size = func->body().size();
        CHECK(body_size < std::numeric_limits<uint32_t>::max());
        section.EmitU32v(static_cast<uint32_t>(body_size + 1));
        section.EmitU8(0);
      } else {
        Binary header;
        header.EmitU32v(
            static_cast<uint32_t>(func->locals().size()));
        for (const auto& decl : func->locals()) {
          header.EmitU32v(decl.second);
          header.EmitType(decl.first);
        }
        size_t total_size = header.length() + func->body().size();
        CHECK(total_size <= std::numeric_limits<uint32_t>::max());
        section.EmitU32v(static_cast<uint32_t>(total_size));
        section.EmitBytes(std::span<const uint8_t>(header.buffer()));
      }
      section.EmitBytes(std::span<const uint8_t>(func->body()));
    }
  });
}

void WasmModuleBuilder::EmitDataSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kDataSectionCode, [&](Binary& section) {
    section.EmitU32v(static_cast<uint32_t>(data_segments_.size()));
    for (const auto& seg : data_segments_) {
      uint8_t shared_flag = seg.is_shared ? 0b1000 : 0;
      if (seg.is_active) {
        if (seg.mem_index == 0) {
          section.EmitU8(kActiveNoIndex | shared_flag);
        } else {
          section.EmitU8(kActiveWithIndex | shared_flag);
          section.EmitU32v(seg.mem_index);
        }
        section.EmitInitExpr(seg.offset);
      } else {
        section.EmitU8(kPassive | shared_flag);
      }
      CHECK(seg.data.size() <= std::numeric_limits<uint32_t>::max());
      section.EmitU32v(static_cast<uint32_t>(seg.data.size()));
      section.EmitBytes(std::span<const uint8_t>(seg.data));
    }
  });
}

void WasmModuleBuilder::EmitExplicitSections(Binary& bin, bool /*debug*/) const {
  for (const auto& exp : explicit_) {
    bin.EmitBytes(std::span<const uint8_t>(exp));
  }
}

void WasmModuleBuilder::EmitNameSection(Binary& bin, bool /*debug*/) const {
  uint32_t num_function_names = 0;
  uint32_t num_functions_with_local_names = 0;
  for (const auto& func : functions_) {
    if (!func->name().empty()) ++num_function_names;
    if (func->NumLocalNames() > 0) ++num_functions_with_local_names;
  }

  if (num_function_names == 0 && num_functions_with_local_names == 0 &&
      name_.empty()) {
    return;
  }

  bin.EmitSection(kUnknownSectionCode, [&](Binary& section) {
    section.EmitString("name");

    if (!name_.empty()) {
      section.EmitSection(kModuleNameCode, [&](Binary& name_section) {
        name_section.EmitString(name_);
      });
    }

    if (num_function_names > 0) {
      section.EmitSection(kFunctionNamesCode, [&](Binary& name_section) {
        name_section.EmitU32v(num_function_names);
        for (const auto& func : functions_) {
          if (func->name().empty()) continue;
          name_section.EmitU32v(func->index());
          name_section.EmitString(func->name());
        }
      });
    }

    if (num_functions_with_local_names > 0) {
      section.EmitSection(kLocalNamesCode, [&](Binary& name_section) {
        name_section.EmitU32v(num_functions_with_local_names);
        for (const auto& func : functions_) {
          if (func->NumLocalNames() == 0) continue;
          name_section.EmitU32v(func->index());
          name_section.EmitU32v(func->NumLocalNames());
          uint32_t name_index = 0;
          for (const auto& ln : func->local_names()) {
            std::visit(
                [&](auto&& val) {
                  using T = std::decay_t<decltype(val)>;
                  if constexpr (std::is_same_v<T, std::string>) {
                    name_section.EmitU32v(name_index);
                    name_section.EmitString(val);
                    ++name_index;
                  } else if constexpr (std::is_same_v<T, int>) {
                    name_index += static_cast<uint32_t>(val);
                  }
                },
                ln);
          }
        }
      });
    }
  });
}

void WasmModuleBuilder::EmitCompilationPrioritySection(Binary& bin,
                                                          bool /*debug*/) const {
  bin.EmitSection(kUnknownSectionCode, [&](Binary& section) {
    section.EmitString("metadata.code.compilation_priority");
    section.EmitU32v(
        static_cast<uint32_t>(compilation_priorities_.size()));
    for (const auto& [index, priority] : compilation_priorities_) {
      section.EmitU32v(index);
      section.EmitU8(0);
      auto comp_leb = WasmUnsignedLeb(priority.compilation_priority);
      auto opt_leb =
          priority.optimization_priority.has_value()
              ? WasmUnsignedLeb(priority.optimization_priority.value())
              : std::vector<uint8_t>{};
      size_t total = comp_leb.size() + opt_leb.size();
      CHECK(total <= std::numeric_limits<uint32_t>::max());
      section.EmitU32v(static_cast<uint32_t>(total));
      section.EmitBytes(std::span<const uint8_t>(comp_leb));
      section.EmitBytes(std::span<const uint8_t>(opt_leb));
    }
  });
}

void WasmModuleBuilder::EmitInstructionFrequencySection(Binary& bin,
                                                           bool /*debug*/) const {
  bin.EmitSection(kUnknownSectionCode, [&](Binary& section) {
    section.EmitString("metadata.code.instr_freq");
    section.EmitU32v(
        static_cast<uint32_t>(instruction_frequencies_.size()));
    for (const auto& [index, frequencies] : instruction_frequencies_) {
      section.EmitU32v(index);
      section.EmitU32v(static_cast<uint32_t>(frequencies.size()));
      for (const auto& freq : frequencies) {
        section.EmitU32v(freq.offset);
        section.EmitU32v(1);
        section.EmitU8(freq.frequency);
      }
    }
  });
}

void WasmModuleBuilder::EmitCallTargetSection(Binary& bin, bool /*debug*/) const {
  bin.EmitSection(kUnknownSectionCode, [&](Binary& section) {
    section.EmitString("metadata.code.call_targets");
    section.EmitU32v(static_cast<uint32_t>(call_targets_.size()));
    for (const auto& [index, targets] : call_targets_) {
      section.EmitU32v(index);
      section.EmitU32v(static_cast<uint32_t>(targets.size()));
      for (const auto& target_offset : targets) {
        section.EmitU32v(target_offset.offset);
        uint32_t hint_length = 0;
        for (const auto& t : target_offset.targets) {
          hint_length += static_cast<uint32_t>(
              WasmUnsignedLeb(t.function_index).size());
          hint_length += static_cast<uint32_t>(
              WasmUnsignedLeb(t.frequency_percent).size());
        }
        section.EmitU32v(hint_length);
        for (const auto& t : target_offset.targets) {
          section.EmitBytes(std::span<const uint8_t>(
              WasmUnsignedLeb(t.function_index)));
          section.EmitBytes(std::span<const uint8_t>(
              WasmUnsignedLeb(t.frequency_percent)));
        }
      }
    }
  });
}

std::vector<uint8_t> WasmModuleBuilder::ToBuffer(bool debug) const {
  StackDepthGuard guard;
  Binary bin;
  bin.EmitHeader();

  if (!types_.empty()) {
    EmitTypeSection(bin, debug);
  }

  if (!imports_.empty()) {
    EmitImportSection(bin, debug);
  }

  if (!functions_.empty()) {
    EmitFunctionSection(bin, debug);
  }

  if (!tables_.empty()) {
    EmitTableSection(bin, debug);
  }

  if (!memories_.empty()) {
    EmitMemorySection(bin, debug);
  }

  if (!tags_.empty()) {
    EmitTagSection(bin, debug);
  }

  if (!stringrefs_.empty()) {
    EmitStringRefSection(bin, debug);
  }

  if (!globals_.empty()) {
    EmitGlobalSection(bin, debug);
  }

  if (!exports_.empty()) {
    EmitExportSection(bin, debug);
  }

  if (start_index_.has_value()) {
    EmitStartSection(bin, debug);
  }

  if (!element_segments_.empty()) {
    EmitElementSection(bin, debug);
  }

  if (std::any_of(data_segments_.begin(), data_segments_.end(),
                   [](const DataSegment& seg) { return !seg.is_active; })) {
    EmitDataCountSection(bin, debug);
  }

  if (!functions_.empty()) {
    EmitCodeSection(bin, debug);
  }

  if (!data_segments_.empty()) {
    EmitDataSection(bin, debug);
  }

  if (!explicit_.empty()) {
    EmitExplicitSections(bin, debug);
  }

  EmitNameSection(bin, debug);

  if (!compilation_priorities_.empty()) {
    EmitCompilationPrioritySection(bin, debug);
  }

  if (!instruction_frequencies_.empty()) {
    EmitInstructionFrequencySection(bin, debug);
  }

  if (!call_targets_.empty()) {
    EmitCallTargetSection(bin, debug);
  }

  std::vector<uint8_t> result = bin.TruncBuffer();
  CHECK(result.size() <= kMaxModuleBytes);
  return result;
}

std::vector<uint8_t> WasmModuleBuilder::ToArray(bool debug) const {
  return ToBuffer(debug);
}

std::vector<uint8_t> WasmI32Const(int32_t val) {
  auto leb = WasmSignedLeb(val, kMaxVarInt32Size);
  std::vector<uint8_t> result = {kExprI32Const};
  result.insert(result.end(), leb.begin(), leb.end());
  return result;
}

std::vector<uint8_t> WasmI64Const(int64_t val) {
  auto leb = WasmSignedLeb64(val, kMaxVarInt64Size);
  std::vector<uint8_t> result = {kExprI64Const};
  result.insert(result.end(), leb.begin(), leb.end());
  return result;
}

std::vector<uint8_t> WasmF32Const(float f) {
  static_assert(sizeof(float) == 4, "Float must be 4 bytes");
  uint8_t bytes[4];
  static_assert(sizeof(bytes) == sizeof(float), "Size mismatch");
  std::memcpy(bytes, &f, sizeof(float));
  return {kExprF32Const, bytes[0], bytes[1], bytes[2], bytes[3]};
}

std::vector<uint8_t> WasmF64Const(double d) {
  static_assert(sizeof(double) == 8, "Double must be 8 bytes");
  uint8_t bytes[8];
  static_assert(sizeof(bytes) == sizeof(double), "Size mismatch");
  std::memcpy(bytes, &d, sizeof(double));
  return {kExprF64Const, bytes[0], bytes[1], bytes[2], bytes[3],
                bytes[4], bytes[5], bytes[6], bytes[7]};
}

std::vector<uint8_t> WasmS128Const(std::span<const uint8_t> bytes) {
  CHECK(bytes.size() == 16);
  std::vector<uint8_t> result = {kSimdPrefix, 0x0c};
  result.insert(result.end(), bytes.begin(), bytes.end());
  return result;
}

std::vector<uint8_t> WasmSignedLeb(int32_t val, int max_len) {
  CHECK(max_len > 0 && max_len <= kMaxVarInt64Size);
  std::vector<uint8_t> res;
  for (int i = 0; i < max_len; ++i) {
    uint8_t v = val & 0x7f;
    if (((v << 25) >> 25) == val) {
      res.push_back(v);
      return res;
    }
    res.push_back(v | 0x80);
    val >>= 7;
  }
  CHECK(false);
  return res;
}

std::vector<uint8_t> WasmSignedLeb64(int64_t val, int max_len) {
  CHECK(max_len > 0 && max_len <= kMaxVarInt64Size);
  std::vector<uint8_t> res;
  for (int i = 0; i < max_len; ++i) {
    uint8_t v = val & 0x7f;
    if (static_cast<int64_t>(static_cast<int8_t>(v)) == val) {
      res.push_back(v);
      return res;
    }
    res.push_back(v | 0x80);
    val >>= 7;
  }
  CHECK(false);
  return res;
}

std::vector<uint8_t> WasmUnsignedLeb(uint32_t val, int max_len) {
  CHECK(max_len > 0 && max_len <= kMaxVarInt64Size);
  std::vector<uint8_t> res;
  for (int i = 0; i < max_len; ++i) {
    uint8_t v = val & 0x7f;
    if (v == val) {
      res.push_back(v);
      return res;
    }
    res.push_back(v | 0x80);
    val >>= 7;
  }
  CHECK(false);
  return res;
}

std::vector<uint8_t> WasmEncodeHeapType(const RefTypeBuilder& type) {
  auto leb = WasmSignedLeb(type.heap_type(), kMaxVarInt32Size);
  std::vector<uint8_t> result;
  if (type.is_shared()) result.push_back(kWasmSharedTypeForm);
  if (type.is_exact()) result.push_back(kWasmExact);
  result.insert(result.end(), leb.begin(), leb.end());
  return result;
}

}  // namespace wasm_builder
}  // namespace cat

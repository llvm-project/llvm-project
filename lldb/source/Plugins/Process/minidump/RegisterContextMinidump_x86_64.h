//===-- RegisterContextMinidump_x86_64.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextMinidump_h_
#define liblldb_RegisterContextMinidump_h_

// Project includes
#include "MinidumpTypes.h"

// Other libraries and framework includes
#include "Plugins/Process/Utility/RegisterInfoInterface.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"

#include "lldb/Target/RegisterContext.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/Support/Endian.h"

// C includes
// C++ includes

namespace lldb_private {

namespace minidump {

// This function receives an ArrayRef pointing to the bytes of the Minidump
// register context and returns a DataBuffer that's ordered by the offsets
// specified in the RegisterInfoInterface argument
// This way we can reuse the already existing register contexts
lldb::DataBufferSP
ConvertMinidumpContextToRegIface(llvm::ArrayRef<uint8_t> source_data,
                                 RegisterInfoInterface *target_reg_interface);

struct Uint128 {
  llvm::support::ulittle64_t high;
  llvm::support::ulittle64_t low;
};

// Reference: see breakpad/crashpad source or WinNT.h
struct MinidumpXMMSaveArea32AMD64 {
  llvm::support::ulittle16_t control_word;
  llvm::support::ulittle16_t status_word;
  uint8_t tag_word;
  uint8_t reserved1;
  llvm::support::ulittle16_t error_opcode;
  llvm::support::ulittle32_t error_offset;
  llvm::support::ulittle16_t error_selector;
  llvm::support::ulittle16_t reserved2;
  llvm::support::ulittle32_t data_offset;
  llvm::support::ulittle16_t data_selector;
  llvm::support::ulittle16_t reserved3;
  llvm::support::ulittle32_t mx_csr;
  llvm::support::ulittle32_t mx_csr_mask;
  Uint128 float_registers[8];
  Uint128 xmm_registers[16];
  uint8_t reserved4[96];
};

struct MinidumpContext_x86_64 {
  // Register parameter home addresses.
  llvm::support::ulittle64_t p1_home;
  llvm::support::ulittle64_t p2_home;
  llvm::support::ulittle64_t p3_home;
  llvm::support::ulittle64_t p4_home;
  llvm::support::ulittle64_t p5_home;
  llvm::support::ulittle64_t p6_home;

  // The context_flags field determines and which parts
  // of the structure are populated (have valid values)
  llvm::support::ulittle32_t context_flags;
  llvm::support::ulittle32_t mx_csr;

  // The next register is included with
  // MinidumpContext_x86_64_Flags::Control
  llvm::support::ulittle16_t cs;

  // The next 4 registers are included with
  // MinidumpContext_x86_64_Flags::Segments
  llvm::support::ulittle16_t ds;
  llvm::support::ulittle16_t es;
  llvm::support::ulittle16_t fs;
  llvm::support::ulittle16_t gs;

  // The next 2 registers are included with
  // MinidumpContext_x86_64_Flags::Control
  llvm::support::ulittle16_t ss;
  llvm::support::ulittle32_t eflags;

  // The next 6 registers are included with
  // MinidumpContext_x86_64_Flags::DebugRegisters
  llvm::support::ulittle64_t dr0;
  llvm::support::ulittle64_t dr1;
  llvm::support::ulittle64_t dr2;
  llvm::support::ulittle64_t dr3;
  llvm::support::ulittle64_t dr6;
  llvm::support::ulittle64_t dr7;

  // The next 4 registers are included with
  // MinidumpContext_x86_64_Flags::Integer
  llvm::support::ulittle64_t rax;
  llvm::support::ulittle64_t rcx;
  llvm::support::ulittle64_t rdx;
  llvm::support::ulittle64_t rbx;

  // The next register is included with
  // MinidumpContext_x86_64_Flags::Control
  llvm::support::ulittle64_t rsp;

  // The next 11 registers are included with
  // MinidumpContext_x86_64_Flags::Integer
  llvm::support::ulittle64_t rbp;
  llvm::support::ulittle64_t rsi;
  llvm::support::ulittle64_t rdi;
  llvm::support::ulittle64_t r8;
  llvm::support::ulittle64_t r9;
  llvm::support::ulittle64_t r10;
  llvm::support::ulittle64_t r11;
  llvm::support::ulittle64_t r12;
  llvm::support::ulittle64_t r13;
  llvm::support::ulittle64_t r14;
  llvm::support::ulittle64_t r15;

  // The next register is included with
  // MinidumpContext_x86_64_Flags::Control
  llvm::support::ulittle64_t rip;

  // The next set of registers are included with
  // MinidumpContext_x86_64_Flags:FloatingPoint
  union FPR {
    MinidumpXMMSaveArea32AMD64 flt_save;
    struct {
      Uint128 header[2];
      Uint128 legacy[8];
      Uint128 xmm[16];
    } sse_registers;
  };

  enum {
    VRCount = 26,
  };

  Uint128 vector_register[VRCount];
  llvm::support::ulittle64_t vector_control;

  // The next 5 registers are included with
  // MinidumpContext_x86_64_Flags::DebugRegisters
  llvm::support::ulittle64_t debug_control;
  llvm::support::ulittle64_t last_branch_to_rip;
  llvm::support::ulittle64_t last_branch_from_rip;
  llvm::support::ulittle64_t last_exception_to_rip;
  llvm::support::ulittle64_t last_exception_from_rip;
};

// For context_flags. These values indicate the type of
// context stored in the structure. The high 24 bits identify the CPU, the
// low 8 bits identify the type of context saved.
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

enum class MinidumpContext_x86_64_Flags : uint32_t {
  x86_64_Flag = 0x00100000,
  Control = x86_64_Flag | 0x00000001,
  Integer = x86_64_Flag | 0x00000002,
  Segments = x86_64_Flag | 0x00000004,
  FloatingPoint = x86_64_Flag | 0x00000008,
  DebugRegisters = x86_64_Flag | 0x00000010,
  XState = x86_64_Flag | 0x00000040,

  Full = Control | Integer | FloatingPoint,
  All = Full | Segments | DebugRegisters,

  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ All)
};

} // end namespace minidump
} // end namespace lldb_private
#endif // liblldb_RegisterContextMinidump_h_

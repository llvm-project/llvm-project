//===-- NativeRegisterContextLinux.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NativeRegisterContextLinux.h"

#include "lldb/Core/RegisterValue.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Host/common/NativeThreadProtocol.h"
#include "lldb/Host/linux/Ptrace.h"

#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"

using namespace lldb_private;
using namespace lldb_private::process_linux;

NativeRegisterContextLinux::NativeRegisterContextLinux(
    NativeThreadProtocol &native_thread, uint32_t concrete_frame_idx,
    RegisterInfoInterface *reg_info_interface_p)
    : NativeRegisterContextRegisterInfo(native_thread, concrete_frame_idx,
                                        reg_info_interface_p) {}

lldb::ByteOrder NativeRegisterContextLinux::GetByteOrder() const {
  // Get the target process whose privileged thread was used for the register
  // read.
  lldb::ByteOrder byte_order = lldb::eByteOrderInvalid;

  NativeProcessProtocolSP process_sp(m_thread.GetProcess());
  if (!process_sp)
    return byte_order;

  if (!process_sp->GetByteOrder(byte_order)) {
    // FIXME log here
  }

  return byte_order;
}

Error NativeRegisterContextLinux::ReadRegisterRaw(uint32_t reg_index,
                                                  RegisterValue &reg_value) {
  const RegisterInfo *const reg_info = GetRegisterInfoAtIndex(reg_index);
  if (!reg_info)
    return Error("register %" PRIu32 " not found", reg_index);

  return DoReadRegisterValue(reg_info->byte_offset, reg_info->name,
                             reg_info->byte_size, reg_value);
}

Error NativeRegisterContextLinux::WriteRegisterRaw(
    uint32_t reg_index, const RegisterValue &reg_value) {
  uint32_t reg_to_write = reg_index;
  RegisterValue value_to_write = reg_value;

  // Check if this is a subregister of a full register.
  const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg_index);
  if (reg_info->invalidate_regs &&
      (reg_info->invalidate_regs[0] != LLDB_INVALID_REGNUM)) {
    Error error;

    RegisterValue full_value;
    uint32_t full_reg = reg_info->invalidate_regs[0];
    const RegisterInfo *full_reg_info = GetRegisterInfoAtIndex(full_reg);

    // Read the full register.
    error = ReadRegister(full_reg_info, full_value);
    if (error.Fail())
      return error;

    lldb::ByteOrder byte_order = GetByteOrder();
    uint8_t dst[RegisterValue::kMaxRegisterByteSize];

    // Get the bytes for the full register.
    const uint32_t dest_size = full_value.GetAsMemoryData(
        full_reg_info, dst, sizeof(dst), byte_order, error);
    if (error.Success() && dest_size) {
      uint8_t src[RegisterValue::kMaxRegisterByteSize];

      // Get the bytes for the source data.
      const uint32_t src_size = reg_value.GetAsMemoryData(
          reg_info, src, sizeof(src), byte_order, error);
      if (error.Success() && src_size && (src_size < dest_size)) {
        // Copy the src bytes to the destination.
        memcpy(dst + (reg_info->byte_offset & 0x1), src, src_size);
        // Set this full register as the value to write.
        value_to_write.SetBytes(dst, full_value.GetByteSize(), byte_order);
        value_to_write.SetType(full_reg_info);
        reg_to_write = full_reg;
      }
    }
  }

  const RegisterInfo *const register_to_write_info_p =
      GetRegisterInfoAtIndex(reg_to_write);
  assert(register_to_write_info_p &&
         "register to write does not have valid RegisterInfo");
  if (!register_to_write_info_p)
    return Error("NativeRegisterContextLinux::%s failed to get RegisterInfo "
                 "for write register index %" PRIu32,
                 __FUNCTION__, reg_to_write);

  return DoWriteRegisterValue(reg_info->byte_offset, reg_info->name, reg_value);
}

Error NativeRegisterContextLinux::ReadGPR() {
  void *buf = GetGPRBuffer();
  if (!buf)
    return Error("GPR buffer is NULL");
  size_t buf_size = GetGPRSize();

  return DoReadGPR(buf, buf_size);
}

Error NativeRegisterContextLinux::WriteGPR() {
  void *buf = GetGPRBuffer();
  if (!buf)
    return Error("GPR buffer is NULL");
  size_t buf_size = GetGPRSize();

  return DoWriteGPR(buf, buf_size);
}

Error NativeRegisterContextLinux::ReadFPR() {
  void *buf = GetFPRBuffer();
  if (!buf)
    return Error("FPR buffer is NULL");
  size_t buf_size = GetFPRSize();

  return DoReadFPR(buf, buf_size);
}

Error NativeRegisterContextLinux::WriteFPR() {
  void *buf = GetFPRBuffer();
  if (!buf)
    return Error("FPR buffer is NULL");
  size_t buf_size = GetFPRSize();

  return DoWriteFPR(buf, buf_size);
}

Error NativeRegisterContextLinux::ReadRegisterSet(void *buf, size_t buf_size,
                                                  unsigned int regset) {
  return NativeProcessLinux::PtraceWrapper(PTRACE_GETREGSET, m_thread.GetID(),
                                           static_cast<void *>(&regset), buf,
                                           buf_size);
}

Error NativeRegisterContextLinux::WriteRegisterSet(void *buf, size_t buf_size,
                                                   unsigned int regset) {
  return NativeProcessLinux::PtraceWrapper(PTRACE_SETREGSET, m_thread.GetID(),
                                           static_cast<void *>(&regset), buf,
                                           buf_size);
}

Error NativeRegisterContextLinux::DoReadRegisterValue(uint32_t offset,
                                                      const char *reg_name,
                                                      uint32_t size,
                                                      RegisterValue &value) {
  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_REGISTERS));

  long data;
  Error error = NativeProcessLinux::PtraceWrapper(
      PTRACE_PEEKUSER, m_thread.GetID(), reinterpret_cast<void *>(offset),
      nullptr, 0, &data);

  if (error.Success())
    // First cast to an unsigned of the same size to avoid sign extension.
    value.SetUInt(static_cast<unsigned long>(data), size);

  LLDB_LOG(log, "{0}: {1:x}", reg_name, data);
  return error;
}

Error NativeRegisterContextLinux::DoWriteRegisterValue(
    uint32_t offset, const char *reg_name, const RegisterValue &value) {
  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_REGISTERS));

  void *buf = reinterpret_cast<void *>(value.GetAsUInt64());
  LLDB_LOG(log, "{0}: {1}", reg_name, buf);

  return NativeProcessLinux::PtraceWrapper(
      PTRACE_POKEUSER, m_thread.GetID(), reinterpret_cast<void *>(offset), buf);
}

Error NativeRegisterContextLinux::DoReadGPR(void *buf, size_t buf_size) {
  return NativeProcessLinux::PtraceWrapper(PTRACE_GETREGS, m_thread.GetID(),
                                           nullptr, buf, buf_size);
}

Error NativeRegisterContextLinux::DoWriteGPR(void *buf, size_t buf_size) {
  return NativeProcessLinux::PtraceWrapper(PTRACE_SETREGS, m_thread.GetID(),
                                           nullptr, buf, buf_size);
}

Error NativeRegisterContextLinux::DoReadFPR(void *buf, size_t buf_size) {
  return NativeProcessLinux::PtraceWrapper(PTRACE_GETFPREGS, m_thread.GetID(),
                                           nullptr, buf, buf_size);
}

Error NativeRegisterContextLinux::DoWriteFPR(void *buf, size_t buf_size) {
  return NativeProcessLinux::PtraceWrapper(PTRACE_SETFPREGS, m_thread.GetID(),
                                           nullptr, buf, buf_size);
}

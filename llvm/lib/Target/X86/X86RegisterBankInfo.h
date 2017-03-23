//===- X86RegisterBankInfo ---------------------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares the targeting of the RegisterBankInfo class for X86.
/// \todo This should be generated by TableGen.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86REGISTERBANKINFO_H
#define LLVM_LIB_TARGET_X86_X86REGISTERBANKINFO_H

#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"

#define GET_REGBANK_DECLARATIONS
#include "X86GenRegisterBank.inc"

namespace llvm {

class LLT;

class X86GenRegisterBankInfo : public RegisterBankInfo {
protected:
#define GET_TARGET_REGBANK_CLASS
#include "X86GenRegisterBank.inc"
#define GET_TARGET_REGBANK_INFO_CLASS
#include "X86GenRegisterBankInfo.def"

  static RegisterBankInfo::PartialMapping PartMappings[];
  static RegisterBankInfo::ValueMapping ValMappings[];

  static PartialMappingIdx getPartialMappingIdx(const LLT &Ty, bool isFP);
  static const RegisterBankInfo::ValueMapping *
  getValueMapping(PartialMappingIdx Idx, unsigned NumOperands);
};

class TargetRegisterInfo;

/// This class provides the information for the target register banks.
class X86RegisterBankInfo final : public X86GenRegisterBankInfo {
private:
  /// Get an instruction mapping.
  /// \return An InstructionMappings with a statically allocated
  /// OperandsMapping.
  static InstructionMapping getSameOperandsMapping(const MachineInstr &MI,
                                                   bool isFP);

public:
  X86RegisterBankInfo(const TargetRegisterInfo &TRI);

  const RegisterBank &
  getRegBankFromRegClass(const TargetRegisterClass &RC) const override;

  InstructionMapping getInstrMapping(const MachineInstr &MI) const override;
};

} // namespace llvm
#endif

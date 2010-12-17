//===-- llvm/MC/MCELFObjectWriter.h - ELF Object Writer ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCELFOBJECTWRITER_H
#define LLVM_MC_MCELFOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
/// \brief Construct a new ELF writer instance.
///
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
MCObjectWriter *createELFObjectWriter(raw_ostream &OS, bool is64Bit,
                                      Triple::OSType OSType, uint16_t EMachine,
                                      bool IsLittleEndian,
                                      bool HasRelocationAddend);
} // End llvm namespace

#endif

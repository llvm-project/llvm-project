#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Options.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/TargetSelect.h"
#include <memory>
#include <list>
#include <unordered_set>

using namespace llvm;
using namespace llvm::object;
using namespace cl;

OptionCategory DwpCategory("Specific Options");
static list<std::string> InputFiles(Positional, OneOrMore,
                                    desc("<input files>"), cat(DwpCategory));

static opt<std::string> OutputFilename(Required, "o", desc("Specify the output file."),
                                      value_desc("filename"), cat(DwpCategory));

static int error(const Twine &Error, const Twine &Context) {
  errs() << Twine("while processing ") + Context + ":\n";
  errs() << Twine("error: ") + Error + "\n";
  return 1;
}

static std::error_code
writeStringsAndOffsets(MCStreamer &Out, StringMap<uint32_t> &Strings,
                       uint32_t &StringOffset, MCSection *StrSection,
                       MCSection *StrOffsetSection, StringRef CurStrSection,
                       StringRef CurStrOffsetSection) {
  // Could possibly produce an error or warning if one of these was non-null but
  // the other was null.
  if (CurStrSection.empty() || CurStrOffsetSection.empty())
    return std::error_code();

  DenseMap<uint32_t, uint32_t> OffsetRemapping;

  DataExtractor Data(CurStrSection, true, 0);
  uint32_t LocalOffset = 0;
  uint32_t PrevOffset = 0;
  while (const char *s = Data.getCStr(&LocalOffset)) {
    StringRef Str(s, LocalOffset - PrevOffset - 1);
    auto Pair = Strings.insert(std::make_pair(Str, StringOffset));
    if (Pair.second) {
      Out.SwitchSection(StrSection);
      Out.EmitBytes(
          StringRef(Pair.first->getKeyData(), Pair.first->getKeyLength() + 1));
      StringOffset += Str.size() + 1;
    }
    OffsetRemapping[PrevOffset] = Pair.first->second;
    PrevOffset = LocalOffset;
  }

  Data = DataExtractor(CurStrOffsetSection, true, 0);

  Out.SwitchSection(StrOffsetSection);

  uint32_t Offset = 0;
  uint64_t Size = CurStrOffsetSection.size();
  while (Offset < Size) {
    auto OldOffset = Data.getU32(&Offset);
    auto NewOffset = OffsetRemapping[OldOffset];
    Out.EmitIntValue(NewOffset, 4);
  }

  return std::error_code();
}

static std::error_code write(MCStreamer &Out, ArrayRef<std::string> Inputs) {
  const auto &MCOFI = *Out.getContext().getObjectFileInfo();
  MCSection *const StrSection = MCOFI.getDwarfStrDWOSection();
  MCSection *const StrOffsetSection = MCOFI.getDwarfStrOffDWOSection();
  const StringMap<MCSection *> KnownSections = {
      {"debug_info.dwo", MCOFI.getDwarfInfoDWOSection()},
      {"debug_types.dwo", MCOFI.getDwarfTypesDWOSection()},
      {"debug_str_offsets.dwo", StrOffsetSection},
      {"debug_str.dwo", StrSection},
      {"debug_loc.dwo", MCOFI.getDwarfLocDWOSection()},
      {"debug_abbrev.dwo", MCOFI.getDwarfAbbrevDWOSection()}};

  StringMap<uint32_t> Strings;
  uint32_t StringOffset = 0;

  for (const auto &Input : Inputs) {
    auto ErrOrObj = object::ObjectFile::createObjectFile(Input);
    if (!ErrOrObj)
      return ErrOrObj.getError();
    const auto *Obj = ErrOrObj->getBinary();
    StringRef CurStrSection;
    StringRef CurStrOffsetSection;
    for (const auto &Section : Obj->sections()) {
      StringRef Name;
      if (std::error_code Err = Section.getName(Name))
        return Err;
      if (MCSection *OutSection =
              KnownSections.lookup(Name.substr(Name.find_first_not_of("._")))) {
        StringRef Contents;
        if (auto Err = Section.getContents(Contents))
          return Err;
        if (OutSection == StrOffsetSection)
          CurStrOffsetSection = Contents;
        else if (OutSection == StrSection)
          CurStrSection = Contents;
        else {
          Out.SwitchSection(OutSection);
          Out.EmitBytes(Contents);
        }
      }
    }
    if (auto Err = writeStringsAndOffsets(Out, Strings, StringOffset,
                                          StrSection, StrOffsetSection,
                                          CurStrSection, CurStrOffsetSection))
      return Err;
  }
  return std::error_code();
}

int main(int argc, char** argv) {

  ParseCommandLineOptions(argc, argv, "merge split dwarf (.dwo) files");

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllTargets();
  llvm::InitializeAllAsmPrinters();

  std::string ErrorStr;
  StringRef Context = "dwarf streamer init";

  Triple TheTriple("x86_64-linux-gnu");

  // Get the target.
  const Target *TheTarget =
      TargetRegistry::lookupTarget("", TheTriple, ErrorStr);
  if (!TheTarget)
    return error(ErrorStr, Context);
  std::string TripleName = TheTriple.getTriple();

  // Create all the MC Objects.
  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  if (!MRI)
    return error(Twine("no register info for target ") + TripleName, Context);

  std::unique_ptr<MCAsmInfo> MAI(TheTarget->createMCAsmInfo(*MRI, TripleName));
  if (!MAI)
    return error("no asm info for target " + TripleName, Context);

  MCObjectFileInfo MOFI;
  MCContext MC(MAI.get(), MRI.get(), &MOFI);
  MOFI.InitMCObjectFileInfo(TheTriple, Reloc::Default, CodeModel::Default,
                             MC);

  auto MAB = TheTarget->createMCAsmBackend(*MRI, TripleName, "");
  if (!MAB)
    return error("no asm backend for target " + TripleName, Context);

  std::unique_ptr<MCInstrInfo> MII(TheTarget->createMCInstrInfo());
  if (!MII)
    return error("no instr info info for target " + TripleName, Context);

  std::unique_ptr<MCSubtargetInfo> MSTI(
      TheTarget->createMCSubtargetInfo(TripleName, "", ""));
  if (!MSTI)
    return error("no subtarget info for target " + TripleName, Context);

  MCCodeEmitter *MCE = TheTarget->createMCCodeEmitter(*MII, *MRI, MC);
  if (!MCE)
    return error("no code emitter for target " + TripleName, Context);

  // Create the output file.
  std::error_code EC;
  raw_fd_ostream OutFile(OutputFilename, EC, sys::fs::F_None);
  if (EC)
    return error(Twine(OutputFilename) + ": " + EC.message(), Context);

  std::unique_ptr<MCStreamer> MS(TheTarget->createMCObjectStreamer(
      TheTriple, MC, *MAB, OutFile, MCE, *MSTI, false,
      /*DWARFMustBeAtTheEnd*/ false));
  if (!MS)
    return error("no object streamer for target " + TripleName, Context);

  if (auto Err = write(*MS, InputFiles))
    return error(Err.message(), "Writing DWP file");

  MS->Finish();
}

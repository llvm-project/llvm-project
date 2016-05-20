//===- llvm/CodeGen/GlobalISel/RegBankSelect.cpp - RegBankSelect -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the RegBankSelect class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/GlobalISel/RegisterBank.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetSubtargetInfo.h"

#define DEBUG_TYPE "regbankselect"

using namespace llvm;

char RegBankSelect::ID = 0;
INITIALIZE_PASS_BEGIN(RegBankSelect, "regbankselect",
                      "Assign register bank of generic virtual registers",
                      false, false);
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfo)
INITIALIZE_PASS_END(RegBankSelect, "regbankselect",
                    "Assign register bank of generic virtual registers", false,
                    false);

RegBankSelect::RegBankSelect(Mode RunningMode)
    : MachineFunctionPass(ID), RBI(nullptr), MRI(nullptr), TRI(nullptr),
      MBFI(nullptr), MBPI(nullptr), OptMode(RunningMode) {
  initializeRegBankSelectPass(*PassRegistry::getPassRegistry());
}

void RegBankSelect::init(MachineFunction &MF) {
  RBI = MF.getSubtarget().getRegBankInfo();
  assert(RBI && "Cannot work without RegisterBankInfo");
  MRI = &MF.getRegInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  if (OptMode != Mode::Fast) {
    MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
    MBPI = &getAnalysis<MachineBranchProbabilityInfo>();
  } else {
    MBFI = nullptr;
    MBPI = nullptr;
  }
  MIRBuilder.setMF(MF);
}

void RegBankSelect::getAnalysisUsage(AnalysisUsage &AU) const {
  if (OptMode != Mode::Fast) {
    // We could preserve the information from these two analysis but
    // the APIs do not allow to do so yet.
    AU.addRequired<MachineBlockFrequencyInfo>();
    AU.addRequired<MachineBranchProbabilityInfo>();
  }
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool RegBankSelect::assignmentMatch(
    unsigned Reg, const RegisterBankInfo::ValueMapping &ValMapping,
    bool &OnlyAssign) const {
  // By default we assume we will have to repair something.
  OnlyAssign = false;
  // Each part of a break down needs to end up in a different register.
  // In other word, Reg assignement does not match.
  if (ValMapping.BreakDown.size() > 1)
    return false;

  const RegisterBank *CurRegBank = RBI->getRegBank(Reg, *MRI, *TRI);
  const RegisterBank *DesiredRegBrank = ValMapping.BreakDown[0].RegBank;
  // Reg is free of assignment, a simple assignment will make the
  // register bank to match.
  OnlyAssign = CurRegBank == nullptr;
  DEBUG(dbgs() << "Does assignment already match: ";
        if (CurRegBank) dbgs() << *CurRegBank; else dbgs() << "none";
        dbgs() << " against ";
        assert(DesiredRegBrank && "The mapping must be valid");
        dbgs() << *DesiredRegBrank << '\n';);
  return CurRegBank == DesiredRegBrank;
}

void RegBankSelect::repairReg(
    MachineOperand &MO, const RegisterBankInfo::ValueMapping &ValMapping,
    RegBankSelect::RepairingPlacement &RepairPt,
    const iterator_range<SmallVectorImpl<unsigned>::iterator> &NewVRegs) {
  assert(ValMapping.BreakDown.size() == 1 && "Not yet implemented");
  // Assume we are repairing a use and thus, the original reg will be
  // the source of the repairing.
  unsigned Src = MO.getReg();
  unsigned Dst = *NewVRegs.begin();
  if (ValMapping.BreakDown.size() == 1)
    MO.setReg(Dst);

  // If we repair a definition, swap the source and destination for
  // the repairing.
  if (MO.isDef())
    std::swap(Src, Dst);

  assert((RepairPt.getNumInsertPoints() == 1 ||
          TargetRegisterInfo::isPhysicalRegister(Dst)) &&
         "We are about to create several defs for Dst");

  // Build the instruction used to repair, then clone it at the right places.
  MachineInstr *MI = MIRBuilder.buildInstr(TargetOpcode::COPY, Dst, Src);
  MI->removeFromParent();
  DEBUG(dbgs() << "Copy: " << PrintReg(Src) << " to: " << PrintReg(Dst)
               << '\n');
  // TODO:
  // Check if MI is legal. if not, we need to legalize all the
  // instructions we are going to insert.
  std::unique_ptr<MachineInstr *[]> NewInstrs(
      new MachineInstr *[RepairPt.getNumInsertPoints()]);
  bool IsFirst = true;
  unsigned Idx = 0;
  for (const std::unique_ptr<InsertPoint> &InsertPt : RepairPt) {
    MachineInstr *CurMI;
    if (IsFirst)
      CurMI = MI;
    else
      CurMI = MIRBuilder.getMF().CloneMachineInstr(MI);
    InsertPt->insert(*CurMI);
    NewInstrs[Idx++] = CurMI;
    IsFirst = false;
  }
  // TODO:
  // Legalize NewInstrs if need be.
}

void RegBankSelect::setSafeInsertionPoint(MachineInstr &InsertPt, bool Before) {
  // Check that we are not looking to insert before a phi.
  // Indeed, we would need more information on what to do.
  // By default that should be all the predecessors, but this is
  // probably not what we want in general.
  assert((!Before || !InsertPt.isPHI()) &&
         "Insertion before phis not implemented");
  // The same kind of observation hold for terminators if we try to
  // insert after them.
  assert((Before || !InsertPt.isTerminator()) &&
         "Insertion after terminatos not implemented");
  if (InsertPt.isPHI()) {
    assert(!Before && "Not supported!!");
    MachineBasicBlock *MBB = InsertPt.getParent();
    assert(MBB && "Insertion point is not in a basic block");
    MachineBasicBlock::iterator FirstNonPHIPt = MBB->getFirstNonPHI();
    if (FirstNonPHIPt == MBB->end()) {
      // If there is not any non-phi instruction, insert at the end of MBB.
      MIRBuilder.setMBB(*MBB, /*Beginning*/ false);
      return;
    }
    // The insertion point before the first non-phi instruction.
    MIRBuilder.setInstr(*FirstNonPHIPt, /*Before*/ true);
    return;
  }
  if (InsertPt.isTerminator()) {
    MachineBasicBlock *MBB = InsertPt.getParent();
    assert(MBB && "Insertion point is not in a basic block");
    MIRBuilder.setInstr(*MBB->getFirstTerminator(), /*Before*/ true);
    return;
  }
  MIRBuilder.setInstr(InsertPt, /*Before*/ Before);
}

void RegBankSelect::tryAvoidingSplit(
    RegBankSelect::RepairingPlacement &RepairPt, const MachineOperand &MO,
    const RegisterBankInfo::ValueMapping &ValMapping) const {
  const MachineInstr &MI = *MO.getParent();
  assert(RepairPt.hasSplit() && "We should not have to adjust for split");
  // Splitting should only occur for PHIs or between terminators,
  // because we only do local repairing.
  assert((MI.isPHI() || MI.isTerminator()) && "Why do we split?");

  assert(&MI.getOperand(RepairPt.getOpIdx()) == &MO &&
         "Repairing placement does not match operand");

  // If we need splitting for phis, that means it is because we
  // could not find an insertion point before the terminators of
  // the predecessor block for this argument. In other words,
  // the input value is defined by one of the terminators.
  assert((!MI.isPHI() || !MO.isDef()) && "Need split for phi def?");

  // We split to repair the use of a phi or a terminator.
  if (!MO.isDef()) {
    if (MI.isTerminator()) {
      assert(&MI != &(*MI.getParent()->getFirstTerminator()) &&
             "Need to split for the first terminator?!");
    } else {
      // For the PHI case, the split may not be actually required.
      // In the copy case, a phi is already a copy on the incoming edge,
      // therefore there is no need to split.
      if (ValMapping.BreakDown.size() == 1)
        // This is a already a copy, there is nothing to do.
        RepairPt.switchTo(RepairingPlacement::RepairingKind::Reassign);
    }
    return;
  }

  // At this point, we need to repair a defintion of a terminator.

  // Technically we need to fix the def of MI on all outgoing
  // edges of MI to keep the repairing local. In other words, we
  // will create several definitions of the same register. This
  // does not work for SSA unless that definition is a physical
  // register.
  // However, there are other cases where we can get away with
  // that while still keeping the repairing local.
  assert(MI.isTerminator() && MO.isDef() &&
         "This code is for the def of a terminator");

  // Since we use RPO traversal, if we need to repair a definition
  // this means this definition could be:
  // 1. Used by PHIs (i.e., this VReg has been visited as part of the
  //    uses of a phi.), or
  // 2. Part of a target specific instruction (i.e., the target applied
  //    some register class constraints when creating the instruction.)
  // If the constraints come for #2, the target said that another mapping
  // is supported so we may just drop them. Indeed, if we do not change
  // the number of registers holding that value, the uses will get fixed
  // when we get to them.
  // Uses in PHIs may have already been proceeded though.
  // If the constraints come for #1, then, those are weak constraints and
  // no actual uses may rely on them. However, the problem remains mainly
  // the same as for #2. If the value stays in one register, we could
  // just switch the register bank of the definition, but we would need to
  // account for a repairing cost for each phi we silently change.
  //
  // In any case, if the value needs to be broken down into several
  // registers, the repairing is not local anymore as we need to patch
  // every uses to rebuild the value in just one register.
  //
  // To summarize:
  // - If the value is in a physical register, we can do the split and
  //   fix locally.
  // Otherwise if the value is in a virtual register:
  // - If the value remains in one register, we do not have to split
  //   just switching the register bank would do, but we need to account
  //   in the repairing cost all the phi we changed.
  // - If the value spans several registers, then we cannot do a local
  //   repairing.

  // Check if this is a physical or virtual register.
  unsigned Reg = MO.getReg();
  if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
    // We are going to split every outgoing edges.
    // Check that this is possible.
    // FIXME: The machine representation is currently broken
    // since it also several terminators in one basic block.
    // Because of that we would technically need a way to get
    // the targets of just one terminator to know which edges
    // we have to split.
    // Assert that we do not hit the ill-formed representation.

    // If there are other terminators before that one, some of
    // the outgoing edges may not be dominated by this definition.
    assert(&MI == &(*MI.getParent()->getFirstTerminator()) &&
           "Do not know which outgoing edges are relevant");
    const MachineInstr *Next = MI.getNextNode();
    assert((!Next || Next->isUnconditionalBranch()) &&
           "Do not know where each terminator ends up");
    if (Next)
      // If the next terminator uses Reg, this means we have
      // to split right after MI and thus we need a way to ask
      // which outgoing edges are affected.
      assert(!Next->readsRegister(Reg) && "Need to split between terminators");
    // We will split all the edges and repair there.
  } else {
    // This is a virtual register defined by a terminator.
    if (ValMapping.BreakDown.size() == 1) {
      // There is nothing to repair, but we may actually lie on
      // the repairing cost because of the PHIs already proceeded
      // as already stated.
      // Though the code will be correct.
      assert(0 && "Repairing cost may not be accurate");
    } else {
      // We need to do non-local repairing. Basically, patch all
      // the uses (i.e., phis) that we already proceeded.
      // For now, just say this mapping is not possible.
      RepairPt.switchTo(RepairingPlacement::RepairingKind::Impossible);
    }
  }
}

RegBankSelect::MappingCost RegBankSelect::computeMapping(
    MachineInstr &MI, const RegisterBankInfo::InstructionMapping &InstrMapping,
    SmallVectorImpl<RepairingPlacement> &RepairPts,
    const RegBankSelect::MappingCost *BestCost) {
  assert((MBFI || !BestCost) && "Costs comparison require MBFI");

  // If mapped with InstrMapping, MI will have the recorded cost.
  MappingCost Cost(MBFI ? MBFI->getBlockFreq(MI.getParent()) : 1);
  bool Saturated = Cost.addLocalCost(InstrMapping.getCost());
  assert(!Saturated && "Possible mapping saturated the cost");
  DEBUG(dbgs() << "Evaluating mapping cost for: " << MI);
  DEBUG(dbgs() << "With: " << InstrMapping << '\n');
  RepairPts.clear();
  if (BestCost && Cost > *BestCost)
    return Cost;

  // Moreover, to realize this mapping, the register bank of each operand must
  // match this mapping. In other words, we may need to locally reassign the
  // register banks. Account for that repairing cost as well.
  // In this context, local means in the surrounding of MI.
  for (unsigned OpIdx = 0, EndOpIdx = MI.getNumOperands(); OpIdx != EndOpIdx;
       ++OpIdx) {
    const MachineOperand &MO = MI.getOperand(OpIdx);
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;
    DEBUG(dbgs() << "Opd" << OpIdx);
    const RegisterBankInfo::ValueMapping &ValMapping =
        InstrMapping.getOperandMapping(OpIdx);
    // If Reg is already properly mapped, this is free.
    bool Assign;
    if (assignmentMatch(Reg, ValMapping, Assign)) {
      DEBUG(dbgs() << " is free (match).\n");
      continue;
    }
    if (Assign) {
      DEBUG(dbgs() << " is free (simple assignment).\n");
      RepairPts.emplace_back(RepairingPlacement(MI, OpIdx, *TRI, *this,
                                                RepairingPlacement::Reassign));
      continue;
    }

    // TODO:
    // Ask the repairing module how much it would cost to get this mapping.
    // Use: NewSources <- Val.
    //     Same size: copy.
    //     Different size: Src1, Src2, ... =
    //           extract_value Val, Src1Begin, Src1Len, Src2Begin, Src2Len, ...
    // Def: Val <- NewDefs
    //     Same size: copy
    //     Different size: Val = build_sequence Defs1, Defs2, ...
    // We should remember that this value is available somewhere else to
    // coalesce the value.

    // Find the insertion point for the repairing code.
    RepairPts.emplace_back(
        RepairingPlacement(MI, OpIdx, *TRI, *this, RepairingPlacement::Insert));
    RepairingPlacement &RepairPt = RepairPts.back();

    // If we need to split a basic block to materialize this insertion point,
    // we may give a higher cost to this mapping.
    // Nevertheless, we may get away with the split, so try that first.
    if (RepairPt.hasSplit())
      tryAvoidingSplit(RepairPt, MO, ValMapping);

    // Check that the materialization of the repairing is possible.
    if (!RepairPt.canMaterialize())
      return MappingCost::ImpossibleCost();

    // Account for the split cost and repair cost.
    // Unless the cost is already saturated or we do not care about the cost.
    if (!BestCost || Saturated)
      continue;

    // To get accurate information we need MBFI and MBPI.
    // Thus, if we end up here this information should be here.
    assert(MBFI && MBPI && "Cost computation requires MBFI and MBPI");

    // Sums up the repairing cost of at each insertion point.
    // TODO: Get the actual repairing cost.
    uint64_t RepairCost = 1;
    // Bias used for splitting: 5%.
    const uint64_t PercentageForBias = 5;
    uint64_t Bias = (RepairCost * PercentageForBias + 99) / 100;
    // We should not need more than a couple of instructions to repair
    // an assignment. In other words, the computation should not
    // overflow because the repairing cost is free of basic block
    // frequency.
    assert(((RepairCost < RepairCost * PercentageForBias) &&
            (RepairCost * PercentageForBias <
             RepairCost * PercentageForBias + 99)) &&
           "Repairing involves more than a billion of instructions?!");
    for (const std::unique_ptr<InsertPoint> &InsertPt : RepairPt) {
      assert(InsertPt->canMaterialize() && "We should not have made it here");
      // We will applied some basic block frequency and those uses uint64_t.
      if (!InsertPt->isSplit())
        Saturated = Cost.addLocalCost(RepairCost);
      else {
        uint64_t CostForInsertPt = RepairCost;
        // Again we shouldn't overflow here givent that
        // CostForInsertPt is frequency free at this point.
        assert(CostForInsertPt + Bias > CostForInsertPt &&
               "Repairing + split bias overflows");
        CostForInsertPt += Bias;
        uint64_t PtCost = InsertPt->frequency(*this) * CostForInsertPt;
        // Check if we just overflowed.
        if ((Saturated = PtCost < CostForInsertPt))
          Cost.saturate();
        else
          Saturated = Cost.addNonLocalCost(PtCost);
      }

      // Stop looking into what it takes to repair, this is already
      // too expensive.
      if (BestCost && Cost > *BestCost)
        return Cost;

      // No need to accumulate more cost information.
      // We need to still gather the repairing information though.
      if (Saturated)
        break;
    }
  }
  return Cost;
}

void RegBankSelect::applyMapping(
    MachineInstr &MI, const RegisterBankInfo::InstructionMapping &InstrMapping,
    SmallVectorImpl<RegBankSelect::RepairingPlacement> &RepairPts) {
  assert(InstrMapping.getID() == RegisterBankInfo::DefaultMappingID &&
         "Rewriting of MI not implemented yet");
  // First, place the repairing code.
  bool NeedRewrite = false;
  SmallVector<unsigned, 8> NewVRegs;
  for (RepairingPlacement &RepairPt : RepairPts) {
    assert(RepairPt.canMaterialize() &&
           RepairPt.getKind() != RepairingPlacement::Impossible &&
           "This mapping is impossible");
    assert(RepairPt.getKind() != RepairingPlacement::None &&
           "This should not make its way in the list");
    unsigned OpIdx = RepairPt.getOpIdx();
    MachineOperand &MO = MI.getOperand(OpIdx);
    const RegisterBankInfo::ValueMapping &ValMapping =
        InstrMapping.getOperandMapping(OpIdx);
    unsigned BreakDownSize = ValMapping.BreakDown.size();
    unsigned Reg = MO.getReg();
    NeedRewrite = BreakDownSize != 1;

    switch (RepairPt.getKind()) {
    case RepairingPlacement::Reassign:
      assert(BreakDownSize == 1 &&
             "Reassignment should only be for simple mapping");
      MRI->setRegBank(Reg, *ValMapping.BreakDown[0].RegBank);
      break;
    case RepairingPlacement::Insert:
      // We need as many new virtual registers as the number of partial mapping.
      for (const RegisterBankInfo::PartialMapping &PartMap :
           ValMapping.BreakDown) {
        unsigned Tmp = MRI->createGenericVirtualRegister(PartMap.Length);
        MRI->setRegBank(Tmp, *PartMap.RegBank);
        NewVRegs.push_back(Tmp);
      }
      repairReg(MO, ValMapping, RepairPt,
                make_range(NewVRegs.end() - BreakDownSize, NewVRegs.end()));
      break;
    default:
      llvm_unreachable("Other kind should not happen");
    }
  }
  // Second, rewrite the instruction.
  (void)NeedRewrite;
  assert(!NeedRewrite && "Not implemented yet");
}

void RegBankSelect::assignInstr(MachineInstr &MI) {
  DEBUG(dbgs() << "Assign: " << MI);
  RegisterBankInfo::InstructionMapping DefaultMapping =
      RBI->getInstrMapping(MI);
  // Remember the repairing placement for all the operands.
  SmallVector<RepairingPlacement, 4> RepairPts;

  MappingCost DefaultCost = computeMapping(MI, DefaultMapping, RepairPts);
  (void)DefaultCost;
  assert(DefaultCost != MappingCost::ImpossibleCost() &&
         "Default mapping is not suited");

  // Make sure the mapping is valid for MI.
  assert(DefaultMapping.verify(MI) && "Invalid instruction mapping");

  DEBUG(dbgs() << "Mapping: " << DefaultMapping << '\n');

  applyMapping(MI, DefaultMapping, RepairPts);

  DEBUG(dbgs() << "Assigned: " << MI);
}

bool RegBankSelect::runOnMachineFunction(MachineFunction &MF) {
  DEBUG(dbgs() << "Assign register banks for: " << MF.getName() << '\n');
  const Function *F = MF.getFunction();
  Mode SaveOptMode = OptMode;
  if (F->hasFnAttribute(Attribute::OptimizeNone))
    OptMode = Mode::Fast;
  init(MF);
  // Walk the function and assign register banks to all operands.
  // Use a RPOT to make sure all registers are assigned before we choose
  // the best mapping of the current instruction.
  ReversePostOrderTraversal<MachineFunction*> RPOT(&MF);
  for (MachineBasicBlock *MBB : RPOT) {
    // Set a sensible insertion point so that subsequent calls to
    // MIRBuilder.
    MIRBuilder.setMBB(*MBB);
    for (MachineInstr &MI : *MBB)
      assignInstr(MI);
  }
  OptMode = SaveOptMode;
  return false;
}

//------------------------------------------------------------------------------
//                  Helper Classes Implementation
//------------------------------------------------------------------------------
RegBankSelect::RepairingPlacement::RepairingPlacement(
    MachineInstr &MI, unsigned OpIdx, const TargetRegisterInfo &TRI, Pass &P,
    RepairingPlacement::RepairingKind Kind)
    // Default is, we are going to insert code to repair OpIdx.
    : Kind(Kind),
      OpIdx(OpIdx),
      CanMaterialize(Kind != RepairingKind::Impossible),
      HasSplit(false),
      P(P) {
  const MachineOperand &MO = MI.getOperand(OpIdx);
  assert(MO.isReg() && "Trying to repair a non-reg operand");

  if (Kind != RepairingKind::Insert)
    return;

  // Repairings for definitions happen after MI, uses happen before.
  bool Before = !MO.isDef();

  // Check if we are done with MI.
  if (!MI.isPHI() && !MI.isTerminator()) {
    addInsertPoint(MI, Before);
    // We are done with the initialization.
    return;
  }

  // Now, look for the special cases.
  if (MI.isPHI()) {
    // - PHI must be the first instructions:
    //   * Before, we have to split the related incoming edge.
    //   * After, move the insertion point past the last phi.
    if (!Before) {
      MachineBasicBlock::iterator It = MI.getParent()->getFirstNonPHI();
      if (It != MI.getParent()->end())
        addInsertPoint(*It, /*Before*/ true);
      else
        addInsertPoint(*(--It), /*Before*/ false);
      return;
    }
    // We repair a use of a phi, we may need to split the related edge.
    MachineBasicBlock &Pred = *MI.getOperand(OpIdx + 1).getMBB();
    // Check if we can move the insertion point prior to the
    // terminators of the predecessor.
    unsigned Reg = MO.getReg();
    MachineBasicBlock::iterator It = Pred.getLastNonDebugInstr();
    for (auto Begin = Pred.begin(); It != Begin && It->isTerminator(); --It)
      if (It->modifiesRegister(Reg, &TRI)) {
        // We cannot hoist the repairing code in the predecessor.
        // Split the edge.
        addInsertPoint(Pred, *MI.getParent());
        return;
      }
    // At this point, we can insert in Pred.

    // - If It is invalid, Pred is empty and we can insert in Pred
    //   wherever we want.
    // - If It is valid, It is the first non-terminator, insert after It.
    if (It == Pred.end())
      addInsertPoint(Pred, /*Beginning*/ false);
    else
      addInsertPoint(*It, /*Before*/ false);
  } else {
    // - Terminators must be the last instructions:
    //   * Before, move the insert point before the first terminator.
    //   * After, we have to split the outcoming edges.
    unsigned Reg = MO.getReg();
    if (Before) {
      // Check whether Reg is defined by any terminator.
      MachineBasicBlock::iterator It = MI;
      for (auto Begin = MI.getParent()->begin();
           --It != Begin && It->isTerminator();)
        if (It->modifiesRegister(Reg, &TRI)) {
          // Insert the repairing code right after the definition.
          addInsertPoint(*It, /*Before*/ false);
          return;
        }
      addInsertPoint(*It, /*Before*/ true);
      return;
    }
    // Make sure Reg is not redefined by other terminators, otherwise
    // we do not know how to split.
    for (MachineBasicBlock::iterator It = MI, End = MI.getParent()->end();
         ++It != End;)
      // The machine verifier should reject this kind of code.
      assert(It->modifiesRegister(Reg, &TRI) && "Do not know where to split");
    // Split each outcoming edges.
    MachineBasicBlock &Src = *MI.getParent();
    for (auto &Succ : Src.successors())
      addInsertPoint(Src, Succ);
  }
}

void RegBankSelect::RepairingPlacement::addInsertPoint(MachineInstr &MI,
                                                       bool Before) {
  addInsertPoint(*new InstrInsertPoint(MI, Before));
}

void RegBankSelect::RepairingPlacement::addInsertPoint(MachineBasicBlock &MBB,
                                                       bool Beginning) {
  addInsertPoint(*new MBBInsertPoint(MBB, Beginning));
}

void RegBankSelect::RepairingPlacement::addInsertPoint(MachineBasicBlock &Src,
                                                       MachineBasicBlock &Dst) {
  addInsertPoint(*new EdgeInsertPoint(Src, Dst, P));
}

void RegBankSelect::RepairingPlacement::addInsertPoint(
    RegBankSelect::InsertPoint &Point) {
  CanMaterialize &= Point.canMaterialize();
  HasSplit |= Point.isSplit();
  InsertPoints.emplace_back(&Point);
}

RegBankSelect::InstrInsertPoint::InstrInsertPoint(MachineInstr &Instr,
                                                  bool Before)
    : InsertPoint(), Instr(Instr), Before(Before) {
  // Since we do not support splitting, we do not need to update
  // liveness and such, so do not do anything with P.
  assert((!Before || !Instr.isPHI()) &&
         "Splitting before phis requires more points");
  assert((!Before || !Instr.getNextNode() || !Instr.getNextNode()->isPHI()) &&
         "Splitting between phis does not make sense");
}

void RegBankSelect::InstrInsertPoint::materialize() {
  if (isSplit()) {
    // Slice and return the beginning of the new block.
    // If we need to split between the terminators, we theoritically
    // need to know where the first and second set of terminators end
    // to update the successors properly.
    // Now, in pratice, we should have a maximum of 2 branch
    // instructions; one conditional and one unconditional. Therefore
    // we know how to update the successor by looking at the target of
    // the unconditional branch.
    // If we end up splitting at some point, then, we should update
    // the liveness information and such. I.e., we would need to
    // access P here.
    // The machine verifier should actually make sure such cases
    // cannot happen.
    llvm_unreachable("Not yet implemented");
  }
  // Otherwise the insertion point is just the current or next
  // instruction depending on Before. I.e., there is nothing to do
  // here.
}

bool RegBankSelect::InstrInsertPoint::isSplit() const {
  // If the insertion point is after a terminator, we need to split.
  if (!Before)
    return Instr.isTerminator();
  // If we insert before an instruction that is after a terminator,
  // we are still after a terminator.
  return Instr.getPrevNode() && Instr.getPrevNode()->isTerminator();
}

uint64_t RegBankSelect::InstrInsertPoint::frequency(const Pass &P) const {
  // Even if we need to split, because we insert between terminators,
  // this split has actually the same frequency as the instruction.
  const MachineBlockFrequencyInfo *MBFI =
      P.getAnalysisIfAvailable<MachineBlockFrequencyInfo>();
  if (!MBFI)
    return 1;
  return MBFI->getBlockFreq(Instr.getParent()).getFrequency();
}

uint64_t RegBankSelect::MBBInsertPoint::frequency(const Pass &P) const {
  const MachineBlockFrequencyInfo *MBFI =
      P.getAnalysisIfAvailable<MachineBlockFrequencyInfo>();
  if (!MBFI)
    return 1;
  return MBFI->getBlockFreq(&MBB).getFrequency();
}

void RegBankSelect::EdgeInsertPoint::materialize() {
  // If we end up repairing twice at the same place before materializing the
  // insertion point, we may think we have to split an edge twice.
  // We should have a factory for the insert point such that identical points
  // are the same instance.
  assert(Src.isSuccessor(DstOrSplit) && DstOrSplit->isPredecessor(&Src) &&
         "This point has already been split");
  MachineBasicBlock *NewBB = Src.SplitCriticalEdge(DstOrSplit, P);
  assert(NewBB && "Invalid call to materialize");
  // We reuse the destination block to hold the information of the new block.
  DstOrSplit = NewBB;
}

uint64_t RegBankSelect::EdgeInsertPoint::frequency(const Pass &P) const {
  const MachineBlockFrequencyInfo *MBFI =
      P.getAnalysisIfAvailable<MachineBlockFrequencyInfo>();
  if (!MBFI)
    return 1;
  if (WasMaterialized)
    return MBFI->getBlockFreq(DstOrSplit).getFrequency();

  const MachineBranchProbabilityInfo *MBPI =
      P.getAnalysisIfAvailable<MachineBranchProbabilityInfo>();
  if (!MBPI)
    return 1;
  // The basic block will be on the edge.
  return (MBFI->getBlockFreq(&Src) * MBPI->getEdgeProbability(&Src, DstOrSplit))
      .getFrequency();
}

bool RegBankSelect::EdgeInsertPoint::canMaterialize() const {
  // If this is not a critical edge, we should not have used this insert
  // point. Indeed, either the successor or the predecessor should
  // have do.
  assert(Src.succ_size() > 1 && DstOrSplit->pred_size() > 1 &&
         "Edge is not critical");
  return Src.canSplitCriticalEdge(DstOrSplit);
}

RegBankSelect::MappingCost::MappingCost(const BlockFrequency &LocalFreq)
    : LocalCost(0), NonLocalCost(0), LocalFreq(LocalFreq.getFrequency()) {}

bool RegBankSelect::MappingCost::addLocalCost(uint64_t Cost) {
  // Check if this overflows.
  if (LocalCost + Cost < LocalCost) {
    saturate();
    return true;
  }
  LocalCost += Cost;
  return isSaturated();
}

bool RegBankSelect::MappingCost::addNonLocalCost(uint64_t Cost) {
  // Check if this overflows.
  if (NonLocalCost + Cost < NonLocalCost) {
    saturate();
    return true;
  }
  NonLocalCost += Cost;
  return isSaturated();
}

bool RegBankSelect::MappingCost::isSaturated() const {
  return LocalCost == UINT64_MAX - 1 && NonLocalCost == UINT64_MAX &&
         LocalFreq == UINT64_MAX;
}

void RegBankSelect::MappingCost::saturate() {
  *this = ImpossibleCost();
  --LocalCost;
}

RegBankSelect::MappingCost RegBankSelect::MappingCost::ImpossibleCost() {
  return MappingCost(UINT64_MAX, UINT64_MAX, UINT64_MAX);
}

bool RegBankSelect::MappingCost::operator<(const MappingCost &Cost) const {
  // Sort out the easy cases.
  if (*this == Cost)
    return false;
  // If one is impossible to realize the other is cheaper unless it is
  // impossible as well.
  if ((*this == ImpossibleCost()) || (Cost == ImpossibleCost()))
    return (*this == ImpossibleCost()) < (Cost == ImpossibleCost());
  // If one is saturated the other is cheaper, unless it is saturated
  // as well.
  if (isSaturated() || Cost.isSaturated())
    return isSaturated() < Cost.isSaturated();
  // At this point we know both costs hold sensible values.

  // If both values have a different base frequency, there is no much
  // we can do but to scale everything.
  // However, if they have the same base frequency we can avoid making
  // complicated computation.
  uint64_t ThisLocalAdjust;
  uint64_t OtherLocalAdjust;
  if (LLVM_LIKELY(LocalFreq == Cost.LocalFreq)) {

    // At this point, we know the local costs are comparable.
    // Do the case that do not involve potential overflow first.
    if (NonLocalCost == Cost.NonLocalCost)
      // Since the non-local costs do not discriminate on the result,
      // just compare the local costs.
      return LocalCost < Cost.LocalCost;

    // The base costs are comparable so we may only keep the relative
    // value to increase our chances of avoiding overflows.
    ThisLocalAdjust = 0;
    OtherLocalAdjust = 0;
    if (LocalCost < Cost.LocalCost)
      OtherLocalAdjust = Cost.LocalCost - LocalCost;
    else
      ThisLocalAdjust = LocalCost - Cost.LocalCost;

  } else {
    ThisLocalAdjust = LocalCost;
    OtherLocalAdjust = Cost.LocalCost;
  }

  // The non-local costs are comparable, just keep the relative value.
  uint64_t ThisNonLocalAdjust = 0;
  uint64_t OtherNonLocalAdjust = 0;
  if (NonLocalCost < Cost.NonLocalCost)
    OtherNonLocalAdjust = Cost.NonLocalCost - NonLocalCost;
  else
    ThisNonLocalAdjust = NonLocalCost - Cost.NonLocalCost;
  // Scale everything to make them comparable.
  uint64_t ThisScaledCost = ThisLocalAdjust * LocalFreq;
  // Check for overflow on that operation.
  bool ThisOverflows = ThisLocalAdjust && (ThisScaledCost < ThisLocalAdjust ||
                                           ThisScaledCost < LocalFreq);
  uint64_t OtherScaledCost = OtherLocalAdjust * Cost.LocalFreq;
  // Check for overflow on the last operation.
  bool OtherOverflows =
      OtherLocalAdjust &&
      (OtherScaledCost < OtherLocalAdjust || OtherScaledCost < Cost.LocalFreq);
  // Add the non-local costs.
  ThisOverflows |= ThisNonLocalAdjust &&
                   ThisScaledCost + ThisNonLocalAdjust < ThisNonLocalAdjust;
  ThisScaledCost += ThisNonLocalAdjust;
  OtherOverflows |= OtherNonLocalAdjust &&
                    OtherScaledCost + OtherNonLocalAdjust < OtherNonLocalAdjust;
  OtherScaledCost += OtherNonLocalAdjust;
  // If both overflows, we cannot compare without additional
  // precision, e.g., APInt. Just give up on that case.
  if (ThisOverflows && OtherOverflows)
    return false;
  // If one overflows but not the other, we can still compare.
  if (ThisOverflows || OtherOverflows)
    return ThisOverflows < OtherOverflows;
  // Otherwise, just compare the values.
  return ThisScaledCost < OtherScaledCost;
}

bool RegBankSelect::MappingCost::operator==(const MappingCost &Cost) const {
  return LocalCost == Cost.LocalCost && NonLocalCost == Cost.NonLocalCost &&
         LocalFreq == Cost.LocalFreq;
}

; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt < %s -instsimplify -S | FileCheck %s

define <4 x i32> @const_folding(<4 x i32> %x) {
; CHECK-LABEL: @const_folding(
; CHECK-NEXT:    ret <4 x i32> zeroinitializer
;
  %shuf = shufflevector <4 x i32> %x, <4 x i32> zeroinitializer, <4 x i32> <i32 5, i32 4, i32 5, i32 4>
  ret <4 x i32> %shuf
}

define <4 x i32> @const_folding1(<4 x i32> %x) {
; CHECK-LABEL: @const_folding1(
; CHECK-NEXT:    ret <4 x i32> <i32 5, i32 5, i32 5, i32 5>
;
  %shuf = shufflevector <4 x i32> <i32 5, i32 4, i32 5, i32 4>, <4 x i32> %x, <4 x i32> zeroinitializer
  ret <4 x i32> %shuf
}

define <4 x i32> @const_folding_negative(<3 x i32> %x) {
; CHECK-LABEL: @const_folding_negative(
; CHECK-NEXT:    [[SHUF:%.*]] = shufflevector <3 x i32> [[X:%.*]], <3 x i32> zeroinitializer, <4 x i32> <i32 2, i32 4, i32 5, i32 4>
; CHECK-NEXT:    ret <4 x i32> [[SHUF]]
;
  %shuf = shufflevector <3 x i32> %x, <3 x i32> zeroinitializer, <4 x i32> <i32 2, i32 4, i32 5, i32 4>
  ret <4 x i32> %shuf
}

define <4 x i32> @splat_operand(<4 x i32> %x) {
; CHECK-LABEL: @splat_operand(
; CHECK-NEXT:    [[SPLAT:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <4 x i32> zeroinitializer
; CHECK-NEXT:    ret <4 x i32> [[SPLAT]]
;
  %splat = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> zeroinitializer
  %shuf = shufflevector <4 x i32> %splat, <4 x i32> undef, <4 x i32> <i32 0, i32 3, i32 2, i32 1>
  ret <4 x i32> %shuf
}

define <4 x i32> @splat_operand1(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @splat_operand1(
; CHECK-NEXT:    [[SPLAT:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> [[Y:%.*]], <4 x i32> zeroinitializer
; CHECK-NEXT:    ret <4 x i32> [[SPLAT]]
;
  %splat = shufflevector <4 x i32> %x, <4 x i32> %y, <4 x i32> zeroinitializer
  %shuf = shufflevector <4 x i32> %splat, <4 x i32> undef, <4 x i32> <i32 0, i32 3, i32 2, i32 1>
  ret <4 x i32> %shuf
}

define <4 x i32> @splat_operand2(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @splat_operand2(
; CHECK-NEXT:    [[SPLAT:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <4 x i32> zeroinitializer
; CHECK-NEXT:    ret <4 x i32> [[SPLAT]]
;
  %splat = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> zeroinitializer
  %shuf = shufflevector <4 x i32> %splat, <4 x i32> %y, <4 x i32> <i32 0, i32 3, i32 2, i32 1>
  ret <4 x i32> %shuf
}

define <4 x i32> @splat_operand3(<4 x i32> %x) {
; CHECK-LABEL: @splat_operand3(
; CHECK-NEXT:    [[SPLAT:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <4 x i32> zeroinitializer
; CHECK-NEXT:    ret <4 x i32> [[SPLAT]]
;
  %splat = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> zeroinitializer
  %shuf = shufflevector <4 x i32> zeroinitializer, <4 x i32> %splat, <4 x i32> <i32 7, i32 6, i32 5, i32 5>
  ret <4 x i32> %shuf
}

define <8 x i32> @splat_operand_negative(<4 x i32> %x) {
; CHECK-LABEL: @splat_operand_negative(
; CHECK-NEXT:    [[SPLAT:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <4 x i32> zeroinitializer
; CHECK-NEXT:    [[SHUF:%.*]] = shufflevector <4 x i32> [[SPLAT]], <4 x i32> undef, <8 x i32> <i32 0, i32 3, i32 2, i32 1, i32 undef, i32 undef, i32 undef, i32 undef>
; CHECK-NEXT:    ret <8 x i32> [[SHUF]]
;
  %splat = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> zeroinitializer
  %shuf = shufflevector <4 x i32> %splat, <4 x i32> undef, <8 x i32> <i32 0, i32 3, i32 2, i32 1, i32 undef, i32 undef, i32 undef, i32 undef>
  ret <8 x i32> %shuf
}

define <4 x i32> @splat_operand_negative2(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @splat_operand_negative2(
; CHECK-NEXT:    [[SPLAT:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <4 x i32> zeroinitializer
; CHECK-NEXT:    [[SHUF:%.*]] = shufflevector <4 x i32> [[SPLAT]], <4 x i32> [[Y:%.*]], <4 x i32> <i32 0, i32 3, i32 4, i32 1>
; CHECK-NEXT:    ret <4 x i32> [[SHUF]]
;
  %splat = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> zeroinitializer
  %shuf = shufflevector <4 x i32> %splat, <4 x i32> %y, <4 x i32> <i32 0, i32 3, i32 4, i32 1>
  ret <4 x i32> %shuf
}

define <4 x i32> @splat_operand_negative3(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @splat_operand_negative3(
; CHECK-NEXT:    [[SPLAT:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <4 x i32> zeroinitializer
; CHECK-NEXT:    [[SHUF:%.*]] = shufflevector <4 x i32> [[Y:%.*]], <4 x i32> [[SPLAT]], <4 x i32> <i32 0, i32 3, i32 4, i32 1>
; CHECK-NEXT:    ret <4 x i32> [[SHUF]]
;
  %splat = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> zeroinitializer
  %shuf = shufflevector <4 x i32> %y, <4 x i32> %splat, <4 x i32> <i32 0, i32 3, i32 4, i32 1>
  ret <4 x i32> %shuf
}

define <4 x i32> @splat_operand_negative4(<4 x i32> %x) {
; CHECK-LABEL: @splat_operand_negative4(
; CHECK-NEXT:    [[SPLAT:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <4 x i32> <i32 2, i32 undef, i32 2, i32 undef>
; CHECK-NEXT:    [[SHUF:%.*]] = shufflevector <4 x i32> [[SPLAT]], <4 x i32> undef, <4 x i32> <i32 0, i32 2, i32 undef, i32 undef>
; CHECK-NEXT:    ret <4 x i32> [[SHUF]]
;
  %splat = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> <i32 2, i32 undef, i32 2, i32 undef>
  %shuf = shufflevector <4 x i32> %splat, <4 x i32> undef, <4 x i32> <i32 0, i32 2, i32 undef, i32 undef>
  ret <4 x i32> %shuf
}

define <4 x i32> @undef_mask(<4 x i32> %x) {
; CHECK-LABEL: @undef_mask(
; CHECK-NEXT:    ret <4 x i32> undef
;
  %shuf = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> undef
  ret <4 x i32> %shuf
}

define <4 x i32> @undef_mask_1(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @undef_mask_1(
; CHECK-NEXT:    ret <4 x i32> undef
;
  %shuf = shufflevector <4 x i32> %x, <4 x i32> %y, <4 x i32> undef
  ret <4 x i32> %shuf
}

define <4 x i32> @identity_mask_0(<4 x i32> %x) {
; CHECK-LABEL: @identity_mask_0(
; CHECK-NEXT:    ret <4 x i32> [[X:%.*]]
;
  %shuf = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i32> %shuf
}

define <4 x i32> @identity_mask_1(<4 x i32> %x) {
; CHECK-LABEL: @identity_mask_1(
; CHECK-NEXT:    ret <4 x i32> [[X:%.*]]
;
  %shuf = shufflevector <4 x i32> undef, <4 x i32> %x, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  ret <4 x i32> %shuf
}

define <4 x i32> @pseudo_identity_mask(<4 x i32> %x) {
; CHECK-LABEL: @pseudo_identity_mask(
; CHECK-NEXT:    ret <4 x i32> [[X:%.*]]
;
  %shuf = shufflevector <4 x i32> %x, <4 x i32> %x, <4 x i32> <i32 0, i32 1, i32 2, i32 7>
  ret <4 x i32> %shuf
}

define <4 x i32> @not_identity_mask(<4 x i32> %x) {
; CHECK-LABEL: @not_identity_mask(
; CHECK-NEXT:    [[SHUF:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> [[X]], <4 x i32> <i32 0, i32 1, i32 2, i32 6>
; CHECK-NEXT:    ret <4 x i32> [[SHUF]]
;
  %shuf = shufflevector <4 x i32> %x, <4 x i32> %x, <4 x i32> <i32 0, i32 1, i32 2, i32 6>
  ret <4 x i32> %shuf
}

; TODO: Should we simplify if the mask has an undef element?

define <4 x i32> @possible_identity_mask(<4 x i32> %x) {
; CHECK-LABEL: @possible_identity_mask(
; CHECK-NEXT:    [[SHUF:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 undef>
; CHECK-NEXT:    ret <4 x i32> [[SHUF]]
;
  %shuf = shufflevector <4 x i32> %x, <4 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 undef>
  ret <4 x i32> %shuf
}

define <4 x i32> @const_operand(<4 x i32> %x) {
; CHECK-LABEL: @const_operand(
; CHECK-NEXT:    ret <4 x i32> <i32 42, i32 45, i32 44, i32 43>
;
  %shuf = shufflevector <4 x i32> <i32 42, i32 43, i32 44, i32 45>, <4 x i32> %x, <4 x i32> <i32 0, i32 3, i32 2, i32 1>
  ret <4 x i32> %shuf
}

define <4 x i32> @merge(<4 x i32> %x) {
; CHECK-LABEL: @merge(
; CHECK-NEXT:    ret <4 x i32> [[X:%.*]]
;
  %lower = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 1, i32 0>
  %upper = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
  %merged = shufflevector <2 x i32> %upper, <2 x i32> %lower, <4 x i32> <i32 3, i32 2, i32 0, i32 1>
  ret <4 x i32> %merged
}

; This crosses lanes from the source op.

define <4 x i32> @not_merge(<4 x i32> %x) {
; CHECK-LABEL: @not_merge(
; CHECK-NEXT:    [[L:%.*]] = shufflevector <4 x i32> [[X:%.*]], <4 x i32> undef, <2 x i32> <i32 0, i32 1>
; CHECK-NEXT:    [[U:%.*]] = shufflevector <4 x i32> [[X]], <4 x i32> undef, <2 x i32> <i32 2, i32 3>
; CHECK-NEXT:    [[MERGED:%.*]] = shufflevector <2 x i32> [[U]], <2 x i32> [[L]], <4 x i32> <i32 3, i32 2, i32 0, i32 1>
; CHECK-NEXT:    ret <4 x i32> [[MERGED]]
;
  %l = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
  %u = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
  %merged = shufflevector <2 x i32> %u, <2 x i32> %l, <4 x i32> <i32 3, i32 2, i32 0, i32 1>
  ret <4 x i32> %merged
}

define <8 x double> @extract_and_concat(<8 x double> %x) {
; CHECK-LABEL: @extract_and_concat(
; CHECK-NEXT:    ret <8 x double> [[X:%.*]]
;
  %s1 = shufflevector <8 x double> %x, <8 x double> undef, <2 x i32> <i32 0, i32 1>
  %s2 = shufflevector <8 x double> %x, <8 x double> undef, <2 x i32> <i32 2, i32 3>
  %s3 = shufflevector <8 x double> %x, <8 x double> undef, <2 x i32> <i32 4, i32 5>
  %s4 = shufflevector <8 x double> %x, <8 x double> undef, <2 x i32> <i32 6, i32 7>
  %s5 = shufflevector <2 x double> %s1, <2 x double> %s2, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %s6 = shufflevector <2 x double> %s3, <2 x double> %s4, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %s7 = shufflevector <4 x double> %s5, <4 x double> %s6, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x double> %s7
}

; This case has intermediate lane crossings.

define <8 x i64> @PR30630(<8 x i64> %x) {
; CHECK-LABEL: @PR30630(
; CHECK-NEXT:    ret <8 x i64> [[X:%.*]]
;
  %s1 = shufflevector <8 x i64> %x, <8 x i64> undef, <2 x i32> <i32 0, i32 4>
  %s2 = shufflevector <8 x i64> %x, <8 x i64> undef, <2 x i32> <i32 1, i32 5>
  %s3 = shufflevector <8 x i64> %x, <8 x i64> undef, <2 x i32> <i32 2, i32 6>
  %s4 = shufflevector <8 x i64> %x, <8 x i64> undef, <2 x i32> <i32 3, i32 7>
  %s5 = shufflevector <2 x i64> %s1, <2 x i64> %s2, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %s6 = shufflevector <2 x i64> %s3, <2 x i64> %s4, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %s7 = shufflevector <4 x i64> %s5, <4 x i64> %s6, <8 x i32> <i32 0, i32 2, i32 4, i32 6, i32 1, i32 3, i32 5, i32 7>
  ret <8 x i64> %s7
}


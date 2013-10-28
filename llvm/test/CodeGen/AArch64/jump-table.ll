; RUN: llc -verify-machineinstrs < %s -mtriple=aarch64-none-linux-gnu | FileCheck %s
; RUN: llc -code-model=large -verify-machineinstrs < %s -mtriple=aarch64-none-linux-gnu | FileCheck --check-prefix=CHECK-LARGE %s

define i32 @test_jumptable(i32 %in) {
; CHECK: test_jumptable

  switch i32 %in, label %def [
    i32 0, label %lbl1
    i32 1, label %lbl2
    i32 2, label %lbl3
    i32 4, label %lbl4
  ]
; CHECK: adrp [[JTPAGE:x[0-9]+]], .LJTI0_0
; CHECK: add x[[JT:[0-9]+]], [[JTPAGE]], #:lo12:.LJTI0_0
; CHECK: ldr [[DEST:x[0-9]+]], [x[[JT]], {{x[0-9]+}}, lsl #3]
; CHECK: br [[DEST]]

; CHECK-LARGE: movz x[[JTADDR:[0-9]+]], #:abs_g3:.LJTI0_0
; CHECK-LARGE: movk x[[JTADDR]], #:abs_g2_nc:.LJTI0_0
; CHECK-LARGE: movk x[[JTADDR]], #:abs_g1_nc:.LJTI0_0
; CHECK-LARGE: movk x[[JTADDR]], #:abs_g0_nc:.LJTI0_0
; CHECK-LARGE: ldr [[DEST:x[0-9]+]], [x[[JTADDR]], {{x[0-9]+}}, lsl #3]
; CHECK-LARGE: br [[DEST]]

def:
  ret i32 0

lbl1:
  ret i32 1

lbl2:
  ret i32 2

lbl3:
  ret i32 4

lbl4:
  ret i32 8

}

; CHECK: .rodata

; CHECK: .LJTI0_0:
; CHECK-NEXT: .xword
; CHECK-NEXT: .xword
; CHECK-NEXT: .xword
; CHECK-NEXT: .xword
; CHECK-NEXT: .xword

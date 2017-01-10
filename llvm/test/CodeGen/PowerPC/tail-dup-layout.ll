; RUN: llc -outline-optional-branches -O2 < %s | FileCheck %s
target datalayout = "e-m:e-i64:64-n32:64"
target triple = "powerpc64le-grtev4-linux-gnu"

; Intended layout:
; The outlining flag produces the layout
; test1
; test2
; test3
; test4
; exit
; optional1
; optional2
; optional3
; optional4
; Tail duplication puts test n+1 at the end of optional n
; so optional1 includes a copy of test2 at the end, and branches
; to test3 (at the top) or falls through to optional 2.
; The CHECK statements check for the whole string of tests and exit block,
; and then check that the correct test has been duplicated into the end of
; the optional blocks and that the optional blocks are in the correct order.
;CHECK-LABEL: straight_test:
; test1 may have been merged with entry
;CHECK: mr [[TAGREG:[0-9]+]], 3
;CHECK: andi. {{[0-9]+}}, [[TAGREG]], 1
;CHECK-NEXT: bc 12, 1, [[OPT1LABEL:[._0-9A-Za-z]+]]
;CHECK-NEXT: [[TEST2LABEL:[._0-9A-Za-z]+]]: # %test2
;CHECK-NEXT: rlwinm. {{[0-9]+}}, [[TAGREG]], 0, 30, 30
;CHECK-NEXT: bne 0, [[OPT2LABEL:[._0-9A-Za-z]+]]
;CHECK-NEXT: [[TEST3LABEL:[._0-9A-Za-z]+]]: # %test3
;CHECK-NEXT: rlwinm. {{[0-9]+}}, [[TAGREG]], 0, 29, 29
;CHECK-NEXT: bne 0, .[[OPT3LABEL:[._0-9A-Za-z]+]]
;CHECK-NEXT: [[TEST4LABEL:[._0-9A-Za-z]+]]: # %test4
;CHECK-NEXT: rlwinm. {{[0-9]+}}, [[TAGREG]], 0, 28, 28
;CHECK-NEXT: bne 0, .[[OPT4LABEL:[._0-9A-Za-z]+]]
;CHECK-NEXT: [[EXITLABEL:[._0-9A-Za-z]+]]: # %exit
;CHECK: blr
;CHECK-NEXT: [[OPT1LABEL]]
;CHECK: rlwinm. {{[0-9]+}}, [[TAGREG]], 0, 30, 30
;CHECK-NEXT: beq 0, [[TEST3LABEL]]
;CHECK-NEXT: [[OPT2LABEL]]
;CHECK: rlwinm. {{[0-9]+}}, [[TAGREG]], 0, 29, 29
;CHECK-NEXT: beq 0, [[TEST4LABEL]]
;CHECK-NEXT: [[OPT3LABEL]]
;CHECK: rlwinm. {{[0-9]+}}, [[TAGREG]], 0, 28, 28
;CHECK-NEXT: beq 0, [[EXITLABEL]]
;CHECK-NEXT: [[OPT4LABEL]]
;CHECK: b [[EXITLABEL]]

define void @straight_test(i32 %tag) {
entry:
  br label %test1
test1:
  %tagbit1 = and i32 %tag, 1
  %tagbit1eq0 = icmp eq i32 %tagbit1, 0
  br i1 %tagbit1eq0, label %test2, label %optional1
optional1:
  call void @a()
  call void @a()
  call void @a()
  call void @a()
  br label %test2
test2:
  %tagbit2 = and i32 %tag, 2
  %tagbit2eq0 = icmp eq i32 %tagbit2, 0
  br i1 %tagbit2eq0, label %test3, label %optional2
optional2:
  call void @b()
  call void @b()
  call void @b()
  call void @b()
  br label %test3
test3:
  %tagbit3 = and i32 %tag, 4
  %tagbit3eq0 = icmp eq i32 %tagbit3, 0
  br i1 %tagbit3eq0, label %test4, label %optional3
optional3:
  call void @c()
  call void @c()
  call void @c()
  call void @c()
  br label %test4
test4:
  %tagbit4 = and i32 %tag, 8
  %tagbit4eq0 = icmp eq i32 %tagbit4, 0
  br i1 %tagbit4eq0, label %exit, label %optional4
optional4:
  call void @d()
  call void @d()
  call void @d()
  call void @d()
  br label %exit
exit:
  ret void
}

; The block then2 is not unavoidable, but since it can be tail-duplicated, it
; should be placed as a fallthrough from test2 and copied.
; CHECK-LABEL: avoidable_test:
; CHECK: # %entry
; CHECK: andi.
; CHECK: # %test2
; Make sure then2 falls through from test2
; CHECK-NOT: # %{{[-_a-zA-Z0-9]+}}
; CHECK: # %then2
; CHECK: rlwinm. {{[0-9]+}}, {{[0-9]+}}, 0, 29, 29
; CHECK-NOT: # %{{[-_a-zA-Z0-9]+}}
; CHECK: # %end2
; CHECK: # %else1
; CHECK: bl a
; CHECK: bl a
; Make sure then2 was copied into else1
; CHECK: rlwinm. {{[0-9]+}}, {{[0-9]+}}, 0, 29, 29
; CHECK: # %else2
; CHECK: bl c
define void @avoidable_test(i32 %tag) {
entry:
  br label %test1
test1:
  %tagbit1 = and i32 %tag, 1
  %tagbit1eq0 = icmp eq i32 %tagbit1, 0
  br i1 %tagbit1eq0, label %test2, label %else1, !prof !1 ; %test2 more likely
else1:
  call void @a()
  call void @a()
  br label %then2
test2:
  %tagbit2 = and i32 %tag, 2
  %tagbit2eq0 = icmp eq i32 %tagbit2, 0
  br i1 %tagbit2eq0, label %then2, label %else2, !prof !1 ; %then2 more likely
then2:
  %tagbit3 = and i32 %tag, 4
  %tagbit3eq0 = icmp eq i32 %tagbit3, 0
  br i1 %tagbit3eq0, label %end2, label %end1, !prof !1 ; %end2 more likely
else2:
  call void @c()
  br label %end2
end2:
  ret void
end1:
  call void @d()
  ret void
}

declare void @a()
declare void @b()
declare void @c()
declare void @d()

!1 = !{!"branch_weights", i32 2, i32 1}

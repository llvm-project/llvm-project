; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=x86_64-apple-darwin -march=x86-64 -mcpu=corei7 -mattr=avx | FileCheck %s --check-prefix=CHECK --check-prefix=AVX
; RUN: llc < %s -mtriple=x86_64-apple-darwin -march=x86-64 -mcpu=corei7 -mattr=avx512vl | FileCheck %s --check-prefix=CHECK --check-prefix=AVX512VL

define i64 @test_x86_sse2_cvtsd2si64(<2 x double> %a0) {
; CHECK-LABEL: test_x86_sse2_cvtsd2si64:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vcvtsd2si %xmm0, %rax
; CHECK-NEXT:    retq
  %res = call i64 @llvm.x86.sse2.cvtsd2si64(<2 x double> %a0) ; <i64> [#uses=1]
  ret i64 %res
}
declare i64 @llvm.x86.sse2.cvtsd2si64(<2 x double>) nounwind readnone


define <2 x double> @test_x86_sse2_cvtsi642sd(<2 x double> %a0, i64 %a1) {
; CHECK-LABEL: test_x86_sse2_cvtsi642sd:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vcvtsi2sdq %rdi, %xmm0, %xmm0
; CHECK-NEXT:    retq
  %res = call <2 x double> @llvm.x86.sse2.cvtsi642sd(<2 x double> %a0, i64 %a1) ; <<2 x double>> [#uses=1]
  ret <2 x double> %res
}
declare <2 x double> @llvm.x86.sse2.cvtsi642sd(<2 x double>, i64) nounwind readnone


define i64 @test_x86_sse2_cvttsd2si64(<2 x double> %a0) {
; CHECK-LABEL: test_x86_sse2_cvttsd2si64:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vcvttsd2si %xmm0, %rax
; CHECK-NEXT:    retq
  %res = call i64 @llvm.x86.sse2.cvttsd2si64(<2 x double> %a0) ; <i64> [#uses=1]
  ret i64 %res
}
declare i64 @llvm.x86.sse2.cvttsd2si64(<2 x double>) nounwind readnone


define i64 @test_x86_sse_cvtss2si64(<4 x float> %a0) {
; CHECK-LABEL: test_x86_sse_cvtss2si64:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vcvtss2si %xmm0, %rax
; CHECK-NEXT:    retq
  %res = call i64 @llvm.x86.sse.cvtss2si64(<4 x float> %a0) ; <i64> [#uses=1]
  ret i64 %res
}
declare i64 @llvm.x86.sse.cvtss2si64(<4 x float>) nounwind readnone


define <4 x float> @test_x86_sse_cvtsi642ss(<4 x float> %a0, i64 %a1) {
; CHECK-LABEL: test_x86_sse_cvtsi642ss:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vcvtsi2ssq %rdi, %xmm0, %xmm0
; CHECK-NEXT:    retq
  %res = call <4 x float> @llvm.x86.sse.cvtsi642ss(<4 x float> %a0, i64 %a1) ; <<4 x float>> [#uses=1]
  ret <4 x float> %res
}
declare <4 x float> @llvm.x86.sse.cvtsi642ss(<4 x float>, i64) nounwind readnone


define i64 @test_x86_sse_cvttss2si64(<4 x float> %a0) {
; CHECK-LABEL: test_x86_sse_cvttss2si64:
; CHECK:       ## BB#0:
; CHECK-NEXT:    vcvttss2si %xmm0, %rax
; CHECK-NEXT:    retq
  %res = call i64 @llvm.x86.sse.cvttss2si64(<4 x float> %a0) ; <i64> [#uses=1]
  ret i64 %res
}
declare i64 @llvm.x86.sse.cvttss2si64(<4 x float>) nounwind readnone

define <4 x double> @test_x86_avx_vzeroall(<4 x double> %a, <4 x double> %b) {
; AVX-LABEL: test_x86_avx_vzeroall:
; AVX:       ## BB#0:
; AVX-NEXT:    vaddpd %ymm1, %ymm0, %ymm0
; AVX-NEXT:    vmovupd %ymm0, -{{[0-9]+}}(%rsp) ## 32-byte Spill
; AVX-NEXT:    vzeroall
; AVX-NEXT:    vmovups -{{[0-9]+}}(%rsp), %ymm0 ## 32-byte Reload
; AVX-NEXT:    retq
;
; AVX512VL-LABEL: test_x86_avx_vzeroall:
; AVX512VL:       ## BB#0:
; AVX512VL-NEXT:    vaddpd %ymm1, %ymm0, %ymm16
; AVX512VL-NEXT:    vzeroall
; AVX512VL-NEXT:    vmovapd %ymm16, %ymm0
; AVX512VL-NEXT:    retq
  %c = fadd <4 x double> %a, %b
  call void @llvm.x86.avx.vzeroall()
  ret <4 x double> %c
}
declare void @llvm.x86.avx.vzeroall() nounwind

define <4 x double> @test_x86_avx_vzeroupper(<4 x double> %a, <4 x double> %b) {
; AVX-LABEL: test_x86_avx_vzeroupper:
; AVX:       ## BB#0:
; AVX-NEXT:    vaddpd %ymm1, %ymm0, %ymm0
; AVX-NEXT:    vmovupd %ymm0, -{{[0-9]+}}(%rsp) ## 32-byte Spill
; AVX-NEXT:    vzeroupper
; AVX-NEXT:    vmovups -{{[0-9]+}}(%rsp), %ymm0 ## 32-byte Reload
; AVX-NEXT:    retq
;
; AVX512VL-LABEL: test_x86_avx_vzeroupper:
; AVX512VL:       ## BB#0:
; AVX512VL-NEXT:    vaddpd %ymm1, %ymm0, %ymm16
; AVX512VL-NEXT:    vzeroupper
; AVX512VL-NEXT:    vmovapd %ymm16, %ymm0
; AVX512VL-NEXT:    retq
  %c = fadd <4 x double> %a, %b
  call void @llvm.x86.avx.vzeroupper()
  ret <4 x double> %c
}
declare void @llvm.x86.avx.vzeroupper() nounwind

; RUN: opt -verify -S < %s

; Tests the name mangling performed by the codepath following
; getMangledTypeStr(). Only tests that code with the various manglings
; run fine: doesn't actually test the mangling with the type of the
; arguments. Meant to serve as an example-document on how the user
; should do name manglings.

; Exercise the most general case, llvm_anyptr_type, using gc.relocate
; and gc.statepoint. Note that it has nothing to do with gc.*
; functions specifically: any function that accepts llvm_anyptr_type
; will serve the purpose.

; function and integer
define i32* @test_iAny(i32* %v) {
       %tok = call i32 (i1 ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_i1f(i1 ()* @return_i1, i32 0, i32 0, i32 0, i32* %v)
       %v-new = call i32* @llvm.experimental.gc.relocate.p0i32(i32 %tok, i32 4, i32 4)
       ret i32* %v-new
}

; float
define float* @test_fAny(float* %v) {
       %tok = call i32 (i1 ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_i1f(i1 ()* @return_i1, i32 0, i32 0, i32 0, float* %v)
       %v-new = call float* @llvm.experimental.gc.relocate.p0f32(i32 %tok, i32 4, i32 4)
       ret float* %v-new
}

; array of integers
define [3 x i32]* @test_aAny([3 x i32]* %v) {
       %tok = call i32 (i1 ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_i1f(i1 ()* @return_i1, i32 0, i32 0, i32 0, [3 x i32]* %v)
       %v-new = call [3 x i32]* @llvm.experimental.gc.relocate.p0a3i32(i32 %tok, i32 4, i32 4)
       ret [3 x i32]* %v-new
}

; vector of integers
define <3 x i32>* @test_vAny(<3 x i32>* %v) {
       %tok = call i32 (i1 ()*, i32, i32, ...)* @llvm.experimental.gc.statepoint.p0f_i1f(i1 ()* @return_i1, i32 0, i32 0, i32 0, <3 x i32>* %v)
       %v-new = call <3 x i32>* @llvm.experimental.gc.relocate.p0v3i32(i32 %tok, i32 4, i32 4)
       ret <3 x i32>* %v-new
}

declare zeroext i1 @return_i1()
declare i32 @llvm.experimental.gc.statepoint.p0f_i1f(i1 ()*, i32, i32, ...)
declare i32* @llvm.experimental.gc.relocate.p0i32(i32, i32, i32)
declare float* @llvm.experimental.gc.relocate.p0f32(i32, i32, i32)
declare [3 x i32]* @llvm.experimental.gc.relocate.p0a3i32(i32, i32, i32)
declare <3 x i32>* @llvm.experimental.gc.relocate.p0v3i32(i32, i32, i32)

	.file	"matmul.polly.interchanged.ll"
	.section	.rodata.cst8,"aM",@progbits,8
	.align	8
.LCPI0_0:
	.quad	4602678819172646912     # double 0.5
	.text
	.globl	init_array
	.align	16, 0x90
	.type	init_array,@function
init_array:                             # @init_array
	.cfi_startproc
# BB#0:                                 # %entry
	pushq	%rbp
.Ltmp2:
	.cfi_def_cfa_offset 16
.Ltmp3:
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
.Ltmp4:
	.cfi_def_cfa_register %rbp
	xorl	%r8d, %r8d
	vmovsd	.LCPI0_0(%rip), %xmm0
	.align	16, 0x90
.LBB0_1:                                # %polly.loop_preheader3
                                        # =>This Loop Header: Depth=1
                                        #     Child Loop BB0_2 Depth 2
	xorl	%ecx, %ecx
	.align	16, 0x90
.LBB0_2:                                # %polly.loop_header2
                                        #   Parent Loop BB0_1 Depth=1
                                        # =>  This Inner Loop Header: Depth=2
	movl	%ecx, %edx
	imull	%r8d, %edx
	movl	%edx, %esi
	sarl	$31, %esi
	shrl	$22, %esi
	addl	%edx, %esi
	andl	$-1024, %esi            # imm = 0xFFFFFFFFFFFFFC00
	negl	%esi
	movq	%r8, %rax
	shlq	$11, %rax
	leal	1(%rdx,%rsi), %edi
	leaq	(%rax,%rax,2), %rsi
	leaq	1(%rcx), %rdx
	cmpq	$1536, %rdx             # imm = 0x600
	vcvtsi2sdl	%edi, %xmm0, %xmm1
	vmulsd	%xmm0, %xmm1, %xmm1
	vcvtsd2ss	%xmm1, %xmm1, %xmm1
	vmovss	%xmm1, A(%rsi,%rcx,4)
	vmovss	%xmm1, B(%rsi,%rcx,4)
	movq	%rdx, %rcx
	jne	.LBB0_2
# BB#3:                                 # %polly.loop_exit4
                                        #   in Loop: Header=BB0_1 Depth=1
	incq	%r8
	cmpq	$1536, %r8              # imm = 0x600
	jne	.LBB0_1
# BB#4:                                 # %polly.loop_exit
	popq	%rbp
	ret
.Ltmp5:
	.size	init_array, .Ltmp5-init_array
	.cfi_endproc

	.globl	print_array
	.align	16, 0x90
	.type	print_array,@function
print_array:                            # @print_array
	.cfi_startproc
# BB#0:                                 # %entry
	pushq	%rbp
.Ltmp9:
	.cfi_def_cfa_offset 16
.Ltmp10:
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
.Ltmp11:
	.cfi_def_cfa_register %rbp
	pushq	%r15
	pushq	%r14
	pushq	%r12
	pushq	%rbx
.Ltmp12:
	.cfi_offset %rbx, -48
.Ltmp13:
	.cfi_offset %r12, -40
.Ltmp14:
	.cfi_offset %r14, -32
.Ltmp15:
	.cfi_offset %r15, -24
	xorl	%r14d, %r14d
	movl	$C, %r15d
	.align	16, 0x90
.LBB1_1:                                # %for.cond1.preheader
                                        # =>This Loop Header: Depth=1
                                        #     Child Loop BB1_2 Depth 2
	movq	stdout(%rip), %rax
	movq	%r15, %r12
	xorl	%ebx, %ebx
	.align	16, 0x90
.LBB1_2:                                # %for.body3
                                        #   Parent Loop BB1_1 Depth=1
                                        # =>  This Inner Loop Header: Depth=2
	vmovss	(%r12), %xmm0
	vcvtss2sd	%xmm0, %xmm0, %xmm0
	movq	%rax, %rdi
	movl	$.L.str, %esi
	movb	$1, %al
	callq	fprintf
	movslq	%ebx, %rax
	imulq	$1717986919, %rax, %rcx # imm = 0x66666667
	movq	%rcx, %rdx
	shrq	$63, %rdx
	sarq	$37, %rcx
	addl	%edx, %ecx
	imull	$80, %ecx, %ecx
	subl	%ecx, %eax
	cmpl	$79, %eax
	jne	.LBB1_4
# BB#3:                                 # %if.then
                                        #   in Loop: Header=BB1_2 Depth=2
	movq	stdout(%rip), %rsi
	movl	$10, %edi
	callq	fputc
.LBB1_4:                                # %for.inc
                                        #   in Loop: Header=BB1_2 Depth=2
	addq	$4, %r12
	incq	%rbx
	movq	stdout(%rip), %rax
	cmpq	$1536, %rbx             # imm = 0x600
	jne	.LBB1_2
# BB#5:                                 # %for.end
                                        #   in Loop: Header=BB1_1 Depth=1
	movl	$10, %edi
	movq	%rax, %rsi
	callq	fputc
	addq	$6144, %r15             # imm = 0x1800
	incq	%r14
	cmpq	$1536, %r14             # imm = 0x600
	jne	.LBB1_1
# BB#6:                                 # %for.end12
	popq	%rbx
	popq	%r12
	popq	%r14
	popq	%r15
	popq	%rbp
	ret
.Ltmp16:
	.size	print_array, .Ltmp16-print_array
	.cfi_endproc

	.section	.rodata.cst8,"aM",@progbits,8
	.align	8
.LCPI2_0:
	.quad	4602678819172646912     # double 0.5
	.text
	.globl	main
	.align	16, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# BB#0:                                 # %entry
	pushq	%rbp
.Ltmp20:
	.cfi_def_cfa_offset 16
.Ltmp21:
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
.Ltmp22:
	.cfi_def_cfa_register %rbp
	pushq	%r14
	pushq	%rbx
.Ltmp23:
	.cfi_offset %rbx, -32
.Ltmp24:
	.cfi_offset %r14, -24
	xorl	%ebx, %ebx
	vmovsd	.LCPI2_0(%rip), %xmm0
	.align	16, 0x90
.LBB2_1:                                # %polly.loop_preheader3.i
                                        # =>This Loop Header: Depth=1
                                        #     Child Loop BB2_2 Depth 2
	xorl	%ecx, %ecx
	.align	16, 0x90
.LBB2_2:                                # %polly.loop_header2.i
                                        #   Parent Loop BB2_1 Depth=1
                                        # =>  This Inner Loop Header: Depth=2
	movl	%ecx, %edx
	imull	%ebx, %edx
	movl	%edx, %esi
	sarl	$31, %esi
	shrl	$22, %esi
	addl	%edx, %esi
	andl	$-1024, %esi            # imm = 0xFFFFFFFFFFFFFC00
	negl	%esi
	movq	%rbx, %rax
	shlq	$11, %rax
	leal	1(%rdx,%rsi), %edi
	leaq	(%rax,%rax,2), %rsi
	leaq	1(%rcx), %rdx
	cmpq	$1536, %rdx             # imm = 0x600
	vcvtsi2sdl	%edi, %xmm0, %xmm1
	vmulsd	%xmm0, %xmm1, %xmm1
	vcvtsd2ss	%xmm1, %xmm1, %xmm1
	vmovss	%xmm1, A(%rsi,%rcx,4)
	vmovss	%xmm1, B(%rsi,%rcx,4)
	movq	%rdx, %rcx
	jne	.LBB2_2
# BB#3:                                 # %polly.loop_exit4.i
                                        #   in Loop: Header=BB2_1 Depth=1
	incq	%rbx
	cmpq	$1536, %rbx             # imm = 0x600
	jne	.LBB2_1
# BB#4:                                 # %polly.loop_preheader3.preheader
	movl	$C, %r14d
	movl	$C, %edi
	xorl	%esi, %esi
	movl	$9437184, %edx          # imm = 0x900000
	callq	memset
	xorl	%eax, %eax
	.align	16, 0x90
.LBB2_5:                                # %polly.loop_preheader17
                                        # =>This Loop Header: Depth=1
                                        #     Child Loop BB2_10 Depth 2
                                        #       Child Loop BB2_8 Depth 3
	movl	$B, %ebx
	xorl	%edx, %edx
	.align	16, 0x90
.LBB2_10:                               # %polly.loop_preheader24
                                        #   Parent Loop BB2_5 Depth=1
                                        # =>  This Loop Header: Depth=2
                                        #       Child Loop BB2_8 Depth 3
	leaq	(%rax,%rax,2), %rcx
	shlq	$11, %rcx
	vmovss	A(%rcx,%rdx,4), %xmm0
	movl	$1536, %esi             # imm = 0x600
	movq	%r14, %rdi
	movq	%rbx, %rcx
	.align	16, 0x90
.LBB2_8:                                # %polly.loop_header23
                                        #   Parent Loop BB2_5 Depth=1
                                        #     Parent Loop BB2_10 Depth=2
                                        # =>    This Inner Loop Header: Depth=3
	vmulss	(%rcx), %xmm0, %xmm1
	vaddss	(%rdi), %xmm1, %xmm1
	vmovss	%xmm1, (%rdi)
	addq	$4, %rdi
	addq	$4, %rcx
	decq	%rsi
	jne	.LBB2_8
# BB#9:                                 # %polly.loop_exit25
                                        #   in Loop: Header=BB2_10 Depth=2
	addq	$6144, %rbx             # imm = 0x1800
	incq	%rdx
	cmpq	$1536, %rdx             # imm = 0x600
	jne	.LBB2_10
# BB#6:                                 # %polly.loop_exit18
                                        #   in Loop: Header=BB2_5 Depth=1
	addq	$6144, %r14             # imm = 0x1800
	incq	%rax
	cmpq	$1536, %rax             # imm = 0x600
	jne	.LBB2_5
# BB#7:                                 # %polly.loop_exit11
	xorl	%eax, %eax
	popq	%rbx
	popq	%r14
	popq	%rbp
	ret
.Ltmp25:
	.size	main, .Ltmp25-main
	.cfi_endproc

	.type	A,@object               # @A
	.comm	A,9437184,16
	.type	B,@object               # @B
	.comm	B,9437184,16
	.type	.L.str,@object          # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	 "%lf "
	.size	.L.str, 5

	.type	C,@object               # @C
	.comm	C,9437184,16

	.section	".note.GNU-stack","",@progbits

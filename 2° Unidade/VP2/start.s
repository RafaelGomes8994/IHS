.syntax		unified
.cpu		cortex-m3
.fpu		softvfp
.thumb

.section	.text.bootloader

.global		_vector
_vector:
	.word	_estack
	.word	_start
	.align	9

.thumb_func
_data_init:
	ldr	r0, =_sdata
	ldr	r1, =_edata
	ldr	r2, =_sidata
	movs	r3, #0
	b 	LoopCopyDataInit
	CopyDataInit:
		ldr	r4, [r2, r3]
		str	r4, [r0, r3]
		adds	r3, r3, #4
	LoopCopyDataInit:
		adds	r4, r0, r3
		cmp	r4, r1
		bcc	CopyDataInit
	ldr	r2, =_sbss
	ldr	r4, =_ebss
	movs	r3, #0
	b	LoopFillZerobss
	FillZerobss:
		str	r3, [r2]
		adds	r2, r2, #4
	LoopFillZerobss:
		cmp	r2, r4
		bcc	FillZerobss
	bx lr

.thumb_func
.global		_start
_start:
	bl	_data_init
	bl	_init_stdio
	bl 	main
	bl	_exit

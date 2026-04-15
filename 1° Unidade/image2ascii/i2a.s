.intel_syntax noprefix


.section .note.GNU-stack,"",@progbits # Remove executable stack permission

# CONSTANTS

.equ SIZE_OF_BYTE, 1
.equ SIZE_OF_UINT32, 4
.equ SIZE_OF_UINT64, 8

.section .bss

	pallete: .space 16 * SIZE_OF_BYTE
	line: .space SIZE_OF_UINT64

.section .data

	WRITE_MODE_FOPEN:  .asciz "w"
	READ_MODE_FOPEN:   .asciz "r"
	READ_HEX_SCANF:    .asciz "%0X"
	READ_INT:          .asciz "%d"
	READ_LL_INT:       .asciz "%lld"
	READ_NEWLINE:      .asciz "\n"
	PRINT_COMPRESSION: .asciz "[%d%]\n"
	PRINT_CHAR:	   .asciz "%c"




.section .text
.global main


#	int compare (void* a, void* b) {
#		if (a[0] < b[0]) return - 1;
#		else if (a[0] > b[0] return 1;
#
#		if (a[1] < b[1]) return -1;
#		else if (a[1] > b[1]) return 1;
#
#		return 0;
#

compare:
	PUSH rbp
	MOV rbp, rsp

	MOV r10d, DWORD PTR [rdi]
	MOV r11d, DWORD PTR [rsi]

	MOV r8d, r10d
	MOV r9d, r11d

	AND r8d, 0xFF00
	AND r9d, 0xFF00

	CMP r8d, r9d

	MOV rax, -1
	JL compare_end

	MOV rax, 1
	JG compare_end

	MOV r8d, r10d
	MOV r9d, r11d

	AND r8d, 0x00FF
	AND r9d, 0x00FF

	CMP r8d, r9d

	JG compare_end

	MOV rax, -1
	JL compare_end

	MOV rax, 0

	compare_end:
	POP rbp
	RET



# INPUT FILE          = rsp + 0
# OUTPUT FILE         = rsp + 8
# SAVE RBX            = rsp + 16
# SAVE IMG COUNTER    = rsp + 24
# TEMP                = rsp + 32
# SAVE R12            = rsp + 48
# IMAGE_BYTES_ADDR    = rsp + 64
# SAVE R14            = rsp + 72
# SAVE R15            = rsp + 80
# SAVE R13            = rsp + 88
# TEMP                = rsp + 96
# TEMP		      = rsp + 104
# TEMP		      = rsp + 112

main:
	PUSH rbp
	MOV rbp, rsp
	SUB rsp, 128

	MOV [rsp+16], rbx
	MOV [rsp+48], r12
	MOV [rsp+72], r14
	MOV [rsp+80], r15
	MOV [rsp+88], r13


	MOV rax, [rsi+8]
	MOV rbx, [rsi+16]

	MOV rdi, rax
	LEA rsi, [rip + READ_MODE_FOPEN]
	CALL fopen@plt
	MOV QWORD PTR [rsp], rax

	MOV rdi, rbx
	LEA rsi, [rip + WRITE_MODE_FOPEN]
	CALL fopen@plt
	MOV QWORD PTR [rsp+8], rax



	LEA rbx, [rip+pallete]
	XOR r12, r12
	PALLETE_LOOP:

		CMP r12, 16
		JE END_PALLETE_LOOP

		MOV rdi, [rsp+0]
		LEA rsi, [rip+READ_HEX_SCANF]
		LEA rdx, [rsp+32]

		CALL fscanf@plt


		MOV rax, [rsp+32]
		MOV BYTE PTR [rbx+r12], al
		ADD r12, SIZE_OF_BYTE

		JMP PALLETE_LOOP
	END_PALLETE_LOOP:

	MOV rdi, [rsp+0]
	LEA rsi, [rip+READ_NEWLINE]
	CALL fscanf@plt

	MOV rdi, [rsp+0]
	LEA rsi, [rip+READ_LL_INT]
	LEA rdx, [rsp+32]

	CALL fscanf@plt
	MOV rax, [rsp+32]
	MOV [rsp+24], rax

	XOR rbx, rbx
	IMAGE_MAIN_LOOP:
		CMP rbx, [rsp+24]
		JE END_IMAGE_MAIN_LOOP

		MOV rdi, [rsp]
		LEA rsi, [rip+READ_LL_INT]
		LEA rdx, [rsp+32]

		CALL fscanf@plt

		MOV rdi, [rsp+32]
		MOV r13, rdi
		LEA rdi, [SIZE_OF_UINT32 * rdi]

		CALL malloc@plt
		MOV [rsp+64], rax
		XOR r12, r12

		IMAGE_GET_BYTE_LOOP:
			CMP r12, r13
			JE END_IMAGE_GET_BYTE_LOOP

			MOV rdi, [rsp]
			LEA rsi, [rip+READ_HEX_SCANF]
			LEA rdx, [rsp+32]

			CALL fscanf@plt

			MOV r14d, DWORD PTR[rsp+32]

			MOV rdi, [rsp]
			LEA rsi, [rip+READ_HEX_SCANF]
			LEA rdx, [rsp+32]

			CALL fscanf@plt

			MOV eax, DWORD PTR[rsp+32]
			SHL rax, 8
			OR r14, rax

			MOV rdi, [rsp]
			LEA rsi, [rip+READ_HEX_SCANF]
			LEA rdx, [rsp+32]

			CALL fscanf@plt

			MOV eax, DWORD PTR [rsp+32]
			SHL rax, 16
			OR r14, rax

			MOV r10, [rsp+64]
			MOV DWORD PTR [r12*SIZE_OF_UINT32 + r10], r14d

			INC r12
			JMP IMAGE_GET_BYTE_LOOP


		END_IMAGE_GET_BYTE_LOOP:

		MOV rdi, [rsp+64]
		MOV rsi, r13
		MOV rdx, SIZE_OF_UINT32
		LEA rcx, [rip+compare]

		CALL qsort@plt

		b:

		MOV rax, [rsp+64]
		DEC r12
		MOV eax, [r12*SIZE_OF_UINT32 + rax]

		MOV rdx, rax
		SHR rdx, 20

		MOV rdi, rax
		AND rdi, 0xFF

		ADD rdx, rdi # width
		MOV [rsp+96], rdx

		AND rax, 0x00FF00
		SHR rax, 8
		INC rax # height

		IMUL rdx, rax
		MOV r10, rdx

		XOR rdx, rdx
		IMUL rax, r13, 3
		IMUL rax, 100
		MOV rdi, r10
		DIV rdi
		MOV rdx, rax

		MOV rdi, [rsp+8]
		LEA rsi, [rip+PRINT_COMPRESSION]


		CALL fprintf@plt

		XOR r12, r12
		XOR r14, r14

		MOV [rsp+32], rbx
		MOV rbx, [rsp+96]
		MOV QWORD PTR [rsp+104], 0


		PRINT_IMAGE_LOOP:
			CMP r12, r13
			JE END_PRINT_IMAGE_LOOP

			MOV rax, [rsp+64]
			MOV eax, DWORD PTR [r12*SIZE_OF_UINT32 + rax]

			MOV r14, rax
			SHR r14, 20

			MOV r10, rax
			SHR r10, 16
			AND r10, 0xF

			LEA r11, [rip+pallete]
			MOV r10b, [r11 + r10]
			AND r10, 0xFF

			MOV [rsp+112], r10


			XOR r15, r15

			PRINT_IMAGE_LOOP_LOOP:
				CMP r15, r14
				JE END_PRINT_IMAGE_LOOP_LOOP

				MOV rax, [rsp+104]
				CMP rbx, rax
				JNE IF_IS_NOT_MULTIPLE

				MOV rdi, [rsp+8]
				LEA rsi, [rip+READ_NEWLINE]

				CALL fprintf@plt

				MOV QWORD PTR [rsp+104], 0

				IF_IS_NOT_MULTIPLE:

				MOV rdi, [rsp+8]
				LEA rsi, [rip+PRINT_CHAR]
				MOV rdx, [rsp+112]

				CALL fprintf@plt


				INC QWORD PTR [rsp+104]
				INC r15
				JMP PRINT_IMAGE_LOOP_LOOP
			END_PRINT_IMAGE_LOOP_LOOP:

			

			INC r12
			JMP PRINT_IMAGE_LOOP
		END_PRINT_IMAGE_LOOP:


		MOV rbx, [rsp+32]
		MOV rdi, [rsp+8]
		LEA rsi, [rip+READ_NEWLINE]

		CALL fprintf@plt


		MOV rdi, [rsp+64]
		CALL free@plt




		INC rbx
		JMP IMAGE_MAIN_LOOP
	END_IMAGE_MAIN_LOOP:




	MOV rbx, [rsp+16]
	MOV r12, [rsp+48]
	MOV r14, [rsp+72]

	ADD rsp, 128
	POP rbp
	MOV rax, 0

	RET
	
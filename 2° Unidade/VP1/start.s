// Definições da arquitetura
.syntax    unified
.cpu       cortex-m3
.fpu       softvfp
.thumb
// Segmento de código
.section   .text.bootloader
// Tabela de vetor de interrupção
.global    _vector
_vector:
	.word  _estack
	.word  _start
	.align 9
// Função start
.thumb_func
_start:
	bl     main
	b      .

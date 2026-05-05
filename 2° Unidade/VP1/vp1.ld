/* Endereços de memória e tamanhos */
RAM_origin   = 0x20000000;
RAM_length   = 60K;
FLASH_origin = 0x08000000;
FLASH_length = 192K;
_estack      = RAM_origin + RAM_length;
/* Mapeamento de memória */
MEMORY {
	RAM   : ORIGIN = RAM_origin,   LENGTH = RAM_length
	FLASH : ORIGIN = FLASH_origin, LENGTH = FLASH_length
}
/* Alocação dos segmentos */
SECTIONS {
	.text   : { *(.text.bootloader) *(.text*) } > FLASH
	.rodata : { *(.rodata*) } > FLASH
	.data   : { *(.data*) } > RAM AT > FLASH
	.bss    : { *(.bss*) } > RAM
}

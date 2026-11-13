// Tipos inteiros padronizados
#include <stdint.h>
// Registrador da USART
volatile uint32_t* const USART = (uint32_t*)(0x40011004);
// Função de impressão serial
void printf_serial(const char* s) {
	// Imprime até o final da string
	while(*s != '\0')
		*USART = (uint32_t)(*s++);
}
// Função principal
int main() {
	// Impressão de mensagem no terminal
	printf_serial("Hello world from QEMU!\n");
	// Retornando sucesso
	return 0;
}

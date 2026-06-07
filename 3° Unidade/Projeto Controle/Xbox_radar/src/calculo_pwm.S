// =============================================================================
// calculo_pwm.S — Cálculo de PWM em Assembly Xtensa LX6 (ESP32)
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// ATENÇÃO: Renomear este arquivo de .asm para .S (S maiúsculo).
// O PlatformIO/GAS processa arquivos .S pelo pré-processador C (cpp),
// o que habilita: macros #define, comentários //, diretivas condicionais.
// Arquivos .asm são ignorados pelo sistema de build do PlatformIO.
//
// Função: uint32_t calcularPWM(uint8_t entrada)
//
// Descrição:
//   Converte um valor de entrada (0–255) em um valor de duty cycle
//   para o periférico LEDC do ESP32 configurado em resolução 10 bits.
//   A fórmula implementada é: saida = entrada * 4
//
//   Tabela de conversão:
//     entrada=0   → saida=0    (motor parado)
//     entrada=128 → saida=512  (~50% — meia velocidade)
//     entrada=200 → saida=800  (~78% — velocidade de cruzeiro do rover)
//     entrada=255 → saida=1020 (~99% — velocidade máxima)
//
//   Limite do LEDC 10-bit: 0–1023. O maior valor uint8_t (255) gera 1020.
//   Não há risco de overflow para o tipo uint32_t retornado.
//
// ABI Xtensa (Windowed Register):
//   a2 = primeiro argumento (entrada) E registrador de retorno
//   a3 = registrador temporário (callee-saved? Não — usamos e restauramos)
//   A função não usa o stack (sem prólogo/epílogo necessário).
// =============================================================================

    .global calcularPWM
    .section .text.calcularPWM, "ax"    // Seção nomeada: permite dead-code strip
    .align  4                            // Alinha a função em 4 bytes (Xtensa)
    .type   calcularPWM, @function       // Metadado ELF: tipo função

calcularPWM:
    // a2 = entrada (uint8_t zero-extendido para 32 bits pelo compilador C)

    movi    a3, 4           // a3 ← 4 (multiplicador)
    mull    a2, a2, a3      // a2 ← a2 × a3  (32×32→32 bits, sem overflow para uint8_t)

    // Resultado em a2: pronto para ser retornado ao caller C/C++
    ret.n                   // Retorno estreito (2 bytes) — otimização de código

    .size calcularPWM, . - calcularPWM  // Metadado ELF: tamanho da função
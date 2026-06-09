#define _GNU_SOURCE

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>

/* ═══════════════════════════════════════════════════════════════
   CONSTANTES E MACROS
   ═══════════════════════════════════════════════════════════════ */

// Tamanho do bloco de bootstrap em bytes: zeragem de GPRs + FPU/XMM + salto
#define SIZE_INITIAL_CODE 192

// Seleciona o encoding x86 correto baseado em quais registradores são "velhos"
// (rax–rdi, índices 0–7) ou "novos" (r8–r15, índices 8–15).
// A ordem dos parâmetros segue a convenção: dest primeiro, src segundo.
#define IF_REGISTERS(codeBothOld, codeReg0Old, codeReg1Old, codeBothNew)    \
    do {                                                                      \
        if      (reg0Index < 8 && reg1Index < 8) { codeBothOld  }            \
        else if (reg0Index < 8)                  { codeReg0Old  }            \
        else if (reg1Index < 8)                  { codeReg1Old  }            \
        else                                     { codeBothNew  }            \
    } while (0)

// Versão para instruções de memória: a ordem do REX prefixo inverte porque
// o ModRM codifica o registrador base (endereço) no campo r/m, não no reg.
#define IF_REGISTERS_MEM(codeBothOld, codeReg1Old, codeReg0Old, codeBothNew) \
    do {                                                                       \
        if      (reg0Index < 8 && reg1Index < 8) { codeBothOld  }             \
        else if (reg1Index < 8)                  { codeReg1Old  }             \
        else if (reg0Index < 8)                  { codeReg0Old  }             \
        else                                     { codeBothNew  }             \
    } while (0)

// Imprime os 12 bits menos significativos de x como string binária.
// Útil para inspecionar opcodes e ModRM manualmente durante debug.
#define PRINT_BIN(x)                                             \
    do {                                                         \
        uint32_t __val = (uint32_t)(x);                          \
        for (int __i = 11; __i >= 0; __i--)                      \
            putchar((__val & (1u << __i)) ? '1' : '0');          \
        putchar('\n');                                           \
    } while (0)

/* ═══════════════════════════════════════════════════════════════
   GLOBAIS
   ═══════════════════════════════════════════════════════════════ */

FILE* inputF;   // arquivo de entrada com as instruções da PICO_MEM em hex
FILE* outputF;  // arquivo de saída com o trace de execução

// Mapeamento: índice lógico do registrador PICO -> número ucontext (REG_*)
// Os 8 primeiros são os GPRs "velhos" do x86-64; os 8 últimos são os "novos".
uint8_t REGS[16] = {
    REG_RAX, REG_RCX, REG_RDX, REG_RBX,   // 0–3
    REG_RSP, REG_RBP, REG_RSI, REG_RDI,   // 4–7
    REG_R8,  REG_R9,  REG_R10, REG_R11,   // 8–11
    REG_R12, REG_R13, REG_R14, REG_R15    // 12–15
};

enum Flags {
    F_GREATER = 4,
    F_LESS    = 2,
    F_EQUAL   = 1
};

char instructionBuffer[30]; // buffer de leitura de uma linha do arquivo de entrada

uint32_t PQP_REGS[16]       = {0};  // espelho em C dos registradores PICO (debug)
uint8_t* PICO_MEM            = NULL; // memória da máquina virtual (256 bytes)
uint32_t translation_order[256];    // PCs na ordem em que foram traduzidos (JIT lazy)
char     translated_strings[256][64]; // strings de disassembly geradas na tradução
uint32_t num_translated      = 0;   // quantas instruções já foram traduzidas

// Estrutura do buffer JIT: código nativo gerado dinamicamente em memória RWX
typedef struct { uint8_t* code; uint64_t pos; size_t size; } Code_t;
Code_t codeMemory = {0};

// Vetores constantes de 128 bits usados como "unidade" para os contadores XMM.
// Cada um tem exatamente 1 num slot diferente, permitindo incrementar um lane
// por instrução via PADDQ com endereçamento RIP-relativo.
uint32_t* const_1_0_0_0; // [1, 0, 0, 0]  -> incrementa lane 0
uint32_t* const_0_1_0_0; // [0, 1, 0, 0]  -> incrementa lane 1
uint32_t* const_0_0_1_0; // [0, 0, 1, 0]  -> incrementa lane 2
uint32_t* const_0_0_0_1; // [0, 0, 0, 1]  -> incrementa lane 3

/* ═══════════════════════════════════════════════════════════════
   CÓDIGO INICIAL (bootstrap JIT)

   Este blob de 192 bytes é copiado para o início do buffer JIT e
   executado UMA VEZ antes de qualquer instrução PICO ser traduzida.
   Ele garante um estado limpo: todos os GPRs, registradores XMM e
   a FPU são zerados. Ao final, um salto incondicional pula os 64
   bytes de dados embutidos e pousa exatamente no offset 192, onde
   começa a área de instruções traduzidas (slots de 16 bytes cada).
   ═══════════════════════════════════════════════════════════════ */
uint8_t initialCode[SIZE_INITIAL_CODE] = {
    // xor de todos os GPRs (48 bytes)
    0x48,0x31,0xC0, 0x48,0x31,0xDB, 0x48,0x31,0xC9, 0x48,0x31,0xD2,
    0x48,0x31,0xF6, 0x48,0x31,0xFF, 0x48,0x31,0xED, 0x48,0x31,0xE4,
    0x4D,0x31,0xC0, 0x4D,0x31,0xC9, 0x4D,0x31,0xD2, 0x4D,0x31,0xDB,
    0x4D,0x31,0xE4, 0x4D,0x31,0xED, 0x4D,0x31,0xF6, 0x4D,0x31,0xFF,

    // fninit - limpar unidade de ponto flutuante (3 bytes)
    0x9B, 0xDB, 0xE2,

    // pxor xmm0 a xmm7 (32 bytes) - Zera os 8 primeiros
    0x66,0x0F,0xEF,0xC0, 0x66,0x0F,0xEF,0xC9,
    0x66,0x0F,0xEF,0xD2, 0x66,0x0F,0xEF,0xDB,
    0x66,0x0F,0xEF,0xE4, 0x66,0x0F,0xEF,0xED,
    0x66,0x0F,0xEF,0xF6, 0x66,0x0F,0xEF,0xFF,

    // pxor xmm8 a xmm15 (40 bytes) - CORRIGIDO: Agora usa o REX 0x45!
    0x66,0x45,0x0F,0xEF,0xC0, 0x66,0x45,0x0F,0xEF,0xC9,
    0x66,0x45,0x0F,0xEF,0xD2, 0x66,0x45,0x0F,0xEF,0xDB,
    0x66,0x45,0x0F,0xEF,0xE4, 0x66,0x45,0x0F,0xEF,0xED,
    0x66,0x45,0x0F,0xEF,0xF6, 0x66,0x45,0x0F,0xEF,0xFF,

    // O PULO DO GATO: Salto incondicional de 64 bytes (0xEB, 0x40)
    // Ele pula a área de dados abaixo e aterrissa exatamante no offset 192!
    0x90, 0x90, 0x90, 0xEB, 0x40,

    // Constantes embutidas a partir do offset 128 (64 bytes totais)
    // Slot 0 (Offset 128)
    0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // Slot 1 (Offset 144)
    0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // Slot 2 (Offset 160)
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // Slot 3 (Offset 176)
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00
};

/* ═══════════════════════════════════════════════════════════════
   HELPERS DE EMISSÃO DE CÓDIGO

   Todas as funções abaixo escrevem bytes diretamente no buffer JIT
   apontado por codeMemory, avançando codeMemory.pos conforme necessário.
   São declaradas inline para eliminar overhead de chamada — cada byte
   emitido conta, já que cada slot tem exatamente 16 bytes disponíveis.
   ═══════════════════════════════════════════════════════════════ */

// Reposiciona o cursor de escrita para um offset absoluto dentro do buffer JIT.
// Usado pela magianegra para sobrescrever o INT3 (0xCC) de um slot específico.
static inline void setApdCAddress(uint64_t addr) { codeMemory.pos = addr; }

// Escreve um único byte no cursor atual e avança o cursor.
static inline void apdC(uint8_t byte) { codeMemory.code[codeMemory.pos++] = byte; }

// Escreve um ponteiro/imediato de 32 bits (little-endian) no cursor atual.
static inline void apdC_PTR32(uint32_t ptr) {
    *(uint32_t*)&codeMemory.code[codeMemory.pos] = ptr;
    codeMemory.pos += 4;
}

// Escreve o endereço de 32 bits de PICO_MEM como deslocamento de base (disp32).
// Usado nas instruções de load/store: MOD=10 (disp32) + base = registrador PICO.
static inline void insertPICO_MEM(uint8_t* ptr) {
    *(uint32_t*)&codeMemory.code[codeMemory.pos] = (uint32_t)(uint64_t)ptr;
    codeMemory.pos += 4;
}

// Escreve um imediato de 16 bits com extensão de sinal para 32 bits.
// Os registradores PICO são de 32 bits, então mov rN, imm16 vira mov eN, imm32.
static inline void apdC_InsertImm16(int16_t imm16) {
    *(uint32_t*)&codeMemory.code[codeMemory.pos] = (uint32_t)(int32_t)imm16;
    codeMemory.pos += 4;
}

// Escreve o deslocamento relativo de 32 bits de um salto (JMP/Jcc rel32).
// O valor já deve ser calculado como (destino - próximo_RIP).
static inline void apdC_InsertEnderecoJmp(int32_t imm32) {
    *(uint32_t*)&codeMemory.code[codeMemory.pos] = (uint32_t)imm32;
    codeMemory.pos += 4;
}

// Emite uma instrução PADDQ xmmN, [RIP + disp32] de exatamente 9 bytes.
//
// O truque: cada instrução PICO ocupa um slot de 16 bytes no buffer JIT.
// Os primeiros 9 bytes são SEMPRE este contador; os 7 bytes restantes
// são o código traduzido da instrução em si.
//
// A instrução PICO no índice (PC/4) incrementa o lane (PC/4 % 4)
// do registrador XMM número (PC/4 / 4). Assim, 64 instruções PICO
// cabem nos 16 registradores XMM × 4 lanes de 32 bits cada.
//
//   xmm0–xmm7  → 9 bytes: NOP + 66 0F FE /5 disp32
//   xmm8–xmm15 → 9 bytes: 66 44 0F FE /5 disp32  (REX.R sobe o reg)
static inline void apdC_InsertCounter(uint32_t PC) {
    uint32_t inst_idx = PC / 4;        // índice global da instrução
    uint32_t xmm_reg  = inst_idx / 4;  // qual XMM acumula este PC
    uint32_t slot_idx = inst_idx % 4;  // qual lane (0–3) dentro do XMM

    if (xmm_reg < 8) {
        // Caso normal (xmm0-xmm7): A instrução RIP-Relativa tem 8 bytes.
        // Injetamos 1 NOP para manter cravado em 9 bytes!
        apdC(0x90);
        apdC(0x66); apdC(0x0F); apdC(0xFE);
        apdC((xmm_reg << 3) | 0x05); // ModRM: 05 = RIP-Relativo (sem byte SIB)
    } else {
        // Seu Caso Especial (xmm8-xmm15): A instrução ganha o REX, somando exatos 9 bytes!
        apdC(0x66); apdC(0x44); apdC(0x0F); apdC(0xFE); // 0x44 = Prefixo REX
        apdC(((xmm_reg - 8) << 3) | 0x05); // ModRM: 05 = RIP-Relativo
    }

    // Distância exata da instrução até a constante na memória
    uint32_t* slots[4] = { const_1_0_0_0, const_0_1_0_0, const_0_0_1_0, const_0_0_0_1 };
    uint8_t*  next_rip = codeMemory.code + codeMemory.pos + 4;
    int32_t   disp32   = (int32_t)((uint8_t*)slots[slot_idx] - next_rip);
    apdC_PTR32((uint32_t)disp32);
}

/* ═══════════════════════════════════════════════════════════════
   SIGNAL HANDLERS
   ═══════════════════════════════════════════════════════════════ */

// Handler de SIGTRAP — o coração do JIT.
//
// Cada slot de 16 bytes começa preenchido com 0xCC (INT3). Na primeira
// execução de uma instrução PICO, o processador dispara SIGTRAP e cai
// aqui. A magianegra então:
//   1. Descobre qual instrução PICO disparou o trap (via RIP).
//   2. Traduz a instrução PICO para x86 nativo de no máximo 7 bytes.
//   3. Prepend o contador PADDQ de 9 bytes, totalizando os 16 do slot.
//   4. Redireciona RIP para o início do slot recém-traduzido.
// Na segunda execução do mesmo slot, o código nativo roda diretamente,
// sem mais traps — é o cache do JIT.
void magianegra(int32_t sig, siginfo_t *si, void* context) {
    ucontext_t* ctx = (ucontext_t*) context;
    
    uint64_t rip = (uint64_t) ctx->uc_mcontext.gregs[REG_RIP];
    uint32_t instCount = ((rip) - (uint64_t) codeMemory.code - SIZE_INITIAL_CODE) >> 4; 
    uint32_t PC = instCount << 2;
    uint8_t opcode = PICO_MEM[PC];


    translation_order[num_translated++] = PC;

    setApdCAddress((rip) - 1 -  (uint64_t) codeMemory.code);
    

    apdC_InsertCounter(PC); //7 bytes
    switch (opcode) {
        case 0x00: // mov rN, imm16  →  MOV r32, imm32 (com extensão de sinal)
            {
                uint8_t reg0Index = PICO_MEM[PC+1]>>4;
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);

                sprintf(translated_strings[PC], "mov_r%u,%08x\n", reg0Index, (int32_t)immValue);
                // printf("[instrucao] \t mov_r%u=%08x\n", reg0Index, (int32_t) immValue);

                // Opcode: B8+rd para rax–rdi; REX.B (0x41) + B8+(rd-8) para r8–r15.
                // Ambos emitem 5 bytes de instrução + NOPs para fechar os 7 bytes livres.
                if (reg0Index <= 7){
                    apdC(0xb8 + reg0Index); apdC_InsertImm16(immValue);
                    apdC(0x90); apdC(0x90); 
                }
                else{
                    apdC(0x41); apdC(0xb8 + reg0Index - 8); apdC_InsertImm16(immValue);
                    apdC(0x90); 
                }
                
                // pos - 16: volta o RIP para o início do slot recém-escrito.
                // Da próxima vez que este PC for executado, roda o código nativo direto.
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
                // printf("codigo vai continuar em: %02x com pos: %ld\n", *((uint8_t*) ctx->uc_mcontext.gregs[REG_RIP]), codeMemory.pos - SIZE_INITIAL_CODE);
            }
            break;

        case 0x01: // mov rDest, rSrc  →  MOV r32, r32
            {
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("[instrucao] \t mov_r%u=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "mov_r%u,r%u\n", reg0Index, reg1Index);

                // ModRM = 0xC0 | (src << 3) | dst  (formato reg-reg, MOD=11)
                // REX prefixo: 0x44 = REX.R (src é r8-r15), 0x41 = REX.B (dst é r8-r15),
                //              0x45 = REX.R + REX.B (ambos r8-r15)
                IF_REGISTERS(
                    apdC(0x89); apdC(0xC0 + reg0Index + (reg1Index << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                    ,
                    apdC(0x44); apdC(0x89); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x41); apdC(0x89); apdC(0xC0 + reg0Index - 8 + (reg1Index << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x45); apdC(0x89); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                );
                
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        
        case 0x02: // mov rDest, [rBase]  →  MOV r32, [rBase + disp32(PICO_MEM)]
            {
                uint8_t reg0Index = PICO_MEM[PC+1]>>4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("[instrucao] \t mov_r%u=[r%u]\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "mov_r%u,[r%u]\n", reg0Index, reg1Index);

                // ModRM MOD=10 (disp32): 0x80 | (dst << 3) | base
                // O disp32 é o endereço absoluto de PICO_MEM truncado para 32 bits —
                // funciona pois todos os mmaps ficam abaixo de 0x20000000 (< 4 GB).
                // IF_REGISTERS_MEM inverte a ordem dos casos porque no load o campo
                // r/m do ModRM carrega o registrador BASE (reg1 = endereço), não dest.
                IF_REGISTERS_MEM(
                    apdC(0x8B); apdC(0x80 + reg1Index + (reg0Index << 3)); insertPICO_MEM(PICO_MEM);
                    apdC(0x90); 
                    , 
                    apdC(0x44); apdC(0x8B); apdC(0x80 + reg1Index + ((reg0Index - 8) << 3)); insertPICO_MEM(PICO_MEM);
                    , 
                    apdC(0x41); apdC(0x8B); apdC(0x80 + reg1Index - 8 + (reg0Index << 3)); insertPICO_MEM(PICO_MEM);
                    , 
                    apdC(0x45); apdC(0x8B); apdC(0x80 + reg1Index - 8 + ((reg0Index - 8) << 3)); insertPICO_MEM(PICO_MEM);
                );

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        
        case 0x03: // mov [rBase], rSrc  →  MOV [rBase + disp32(PICO_MEM)], r32
            {
                uint8_t reg0Index = PICO_MEM[PC+1]>>4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("[instrucao] \t mov_[r%u]=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "mov_[r%u],r%u\n", reg0Index, reg1Index);

                // Espelho do case 0x02 com opcode 0x89 (store) em vez de 0x8B (load).
                // Aqui reg0 é a BASE (endereço) e reg1 é a FONTE — IF_REGISTERS normal.
                IF_REGISTERS(
                    apdC(0x89); apdC(0x80 + reg0Index + (reg1Index << 3)); insertPICO_MEM(PICO_MEM);
                    apdC(0x90); 
                    , 
                    apdC(0x44); apdC(0x89); apdC(0x80 + reg0Index + ((reg1Index - 8) << 3)); insertPICO_MEM(PICO_MEM);
                    , 
                    apdC(0x41); apdC(0x89); apdC(0x80 + reg0Index - 8 + (reg1Index << 3)); insertPICO_MEM(PICO_MEM);
                    , 
                    apdC(0x45); apdC(0x89); apdC(0x80 + reg0Index - 8 + ((reg1Index - 8) << 3)); insertPICO_MEM(PICO_MEM);
                );

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x04: // cmp rA <=> rB  →  CMP r32, r32  (seta flags EFLAGS nativos)
            {
                uint8_t reg0Index = PICO_MEM[PC+1]>>4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("[instrucao] \t cmp_r%u<=>r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "cmp_r%u<=>r%u\n", reg0Index, reg1Index);

                // Opcode 0x39: CMP r/m32, r32. Mesmo esquema de ModRM que o MOV.
                // Os saltos condicionais (0x06–0x08) usam os EFLAGS setados aqui.
                IF_REGISTERS(
                    apdC(0x39); apdC(0xC0 + reg0Index + (reg1Index << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x44); apdC(0x39); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    ,
                    apdC(0x41); apdC(0x39); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x45); apdC(0x39); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                );
                
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x05: // jmp imm16  →  JMP rel32
            {   
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);
                // PC_target é o endereço PICO de destino (wraps em 64K)
                uint32_t PC_target = (PC + immValue + 4) & 0xFFFF;

                // JMP rel32 (0xE9): o rel32 é calculado como destino - próximo_RIP.
                // O slot de destino ainda pode ser um INT3 — quando for atingido,
                // a magianegra vai traduzi-lo também (JIT lazy encadeado).
                apdC(0xE9); 

                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + (PC_target *4);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                
                apdC(0x90); apdC(0x90); 

                if (PC_target >= 0xFF) {
                    // printf("[instrucao] \t jmp_\n");
                    sprintf(translated_strings[PC], "jmp_%04x\n", PC_target);
                }
                else {
                    // printf("[instrucao] \t jmp_%04x\n", PC_target);
                    sprintf(translated_strings[PC], "jmp_%04x\n", PC_target);
                }

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        
        case 0x06: // jg imm16  →  JG rel32  (salta se ZF=0 e SF=OF, i.e. maior)
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);
                uint32_t PC_target = (PC + immValue + 4) & 0xFFFF;
                
                // 0x0F 0x8F = JG rel32 (também chamado de JNLE)
                apdC(0x0F); apdC(0x8F);

                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + (PC_target *4);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90); 

                // printf( "[instrucao] \t jg_%04x\n", PC_target);
                sprintf(translated_strings[PC], "jg_%04x\n", PC_target);

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x07: // jl imm16  →  JL rel32  (salta se SF != OF, i.e. menor)
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);
                uint32_t PC_target = (PC + immValue + 4) & 0xFFFF;
                
                // 0x0F 0x8C = JL rel32 (também chamado de JNGE)
                apdC(0x0F); apdC(0x8C);

                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + (PC_target *4);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90); 

                // printf("[instrucao] \t jl_%04x\n", PC_target);
                sprintf(translated_strings[PC], "jl_%04x\n", PC_target);

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x08: // je imm16  →  JE rel32  (salta se ZF=1, i.e. igual)
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);
                uint32_t PC_target = (PC + immValue + 4) & 0xFFFF;
                
                // 0x0F 0x84 = JE rel32 (também chamado de JZ)
                apdC(0x0F); apdC(0x84);

                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + (PC_target *4);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90); 

                // printf("[instrucao] \t je_%04x\n", PC_target);
                sprintf(translated_strings[PC], "je_%04x\n", PC_target);

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x09:
            {   
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("add_r%u+=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "add_r%u+=r%u\n", reg0Index, reg1Index);

                IF_REGISTERS(
                    apdC(0x01); apdC(0xC0 + reg0Index + (reg1Index << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x44); apdC(0x01); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);  
                    ,
                    apdC(0x41); apdC(0x01); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x45); apdC(0x01); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                );

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }   
            break; 

        case 0X0A: 
            {
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("sub_r%u-=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "sub_r%u-=r%u\n", reg0Index, reg1Index);

                IF_REGISTERS(
                    apdC(0x29); apdC(0xC0 + reg0Index + (reg1Index << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x44); apdC(0x29); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    ,
                    apdC(0x41); apdC(0x29); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x45); apdC(0x29); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                );
                
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x0B:
            {
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("and_r%u&=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "and_r%u&=r%u\n", reg0Index, reg1Index);

                IF_REGISTERS(
                    apdC(0x21); apdC(0xC0 + reg0Index + (reg1Index << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                    , 
                    apdC(0x44); apdC(0x21); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    ,
                    apdC(0x41); apdC(0x21); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x45); apdC(0x21); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x0C:
            {
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("or_r%u|=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "or_r%u|=r%u\n", reg0Index, reg1Index);

                IF_REGISTERS(
                    apdC(0x09); apdC(0xC0 + reg0Index + (reg1Index << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x44); apdC(0x09); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    ,
                    apdC(0x41); apdC(0x09); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x45); apdC(0x09); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                );

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x0D:
            {
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("xor_r%u^=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "xor_r%u^=r%u\n", reg0Index, reg1Index);

                IF_REGISTERS(
                    apdC(0x31); apdC(0xC0 + reg0Index + (reg1Index << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x44); apdC(0x31); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    ,
                    apdC(0x41); apdC(0x31); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                    , 
                    apdC(0x45); apdC(0x31); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3));
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x0E:
            {
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t slImm = PICO_MEM[PC+3] & 0xF;

                // printf("sal_r%u<<=%u\n", reg0Index, slImm);
                sprintf(translated_strings[PC], "sal_r%u<<=%u\n", reg0Index, slImm);

                if (reg0Index < 8){
                    apdC(0xC1); apdC(0xE0 + reg0Index); apdC(slImm);
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                }
                else{
                    apdC(0x41); apdC(0xC1); apdC(0xE0 + reg0Index - 8); apdC(slImm);
                    apdC(0x90); apdC(0x90); apdC(0x90); 
                }

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x0F:
            {
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t slImm = PICO_MEM[PC+3] & 0xF;

                // printf("sar_r%u>>=%u\n", reg0Index, slImm);
                sprintf(translated_strings[PC], "sar_r%u>>=%u\n", reg0Index, slImm);

                if (reg0Index < 8){
                    apdC(0xC1); apdC(0xF8 + reg0Index); apdC(slImm);
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); 
                }
                else{
                    apdC(0x41); apdC(0xC1); apdC(0xF8 + reg0Index - 8); apdC(slImm);
                    apdC(0x90); apdC(0x90); apdC(0x90); 
                }

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        default:
            {
                // printf("nenhuma instrucao!\n");
            }
    }



    // printf("\n");
}

// Handler de SIGSEGV — o fim da execução.
//
// O buffer JIT é alocado com tamanho exato para o programa carregado.
// Quando o PC ultrapassa a última instrução, o RIP aponta para além do
// mmap, causando um SIGSEGV. A impressao_do_capeta aproveita isso para:
//   1. Determinar o PC de saída a partir do RIP que faltou.
//   2. Ler os contadores de execução diretamente dos registradores XMM
//      (salvos pelo kernel no bloco fpregs do ucontext).
//   3. Ordenar e imprimir o trace completo com contagens alinhadas.
//   4. Liberar todos os mmaps e sair limpo.
void impressao_do_capeta (int32_t sig, siginfo_t *si, void* context){
    ucontext_t* ctx = (ucontext_t*) context;
    //printCode();

    uint64_t rip = (uint64_t) ctx->uc_mcontext.gregs[REG_RIP];

    // printf("rip: %04X\n", rip);
    // printf("Instcount: %04X\n", instCount);
    uint32_t PC_exit = ((rip) - (uint64_t) codeMemory.code - SIZE_INITIAL_CODE) >> 2;
    // printf("Info: %d\n", si->si_addr);

    // Busca os contadores direto do silício (XMM Registers)
    uint8_t *fp = (uint8_t *)ctx->uc_mcontext.fpregs;
    uint32_t *xmm_space = (uint32_t *)(fp + 160);

// 1. Descobrir a contagem máxima para o Padding
    uint32_t max_count = 0;
    for (uint32_t i = 0; i < num_translated; i++) {
        uint32_t c = xmm_space[translation_order[i] / 4];
        if (c > max_count) {
            max_count = c;
        }
    }

    // 2. Extrai exatamente quantas casas decimais o maior número tem
    int max_digits = snprintf(NULL, 0, "%u", max_count);

    // 3. Montar as strings de formatação absolutas!
    char fmt_inst[64], fmt_exit[64];
    sprintf(fmt_inst, "%%%du:%%04x:%%s", max_digits); 
    sprintf(fmt_exit, "%%%ds:%%04x:exit\n\n", max_digits);

    // 4. RESTAURANDO A ORDENAÇÃO (Ordem Crescente dos PCs)
    for (uint32_t i = 0; i < num_translated; i++) {
        for (uint32_t j = i + 1; j < num_translated; j++) {
            if (translation_order[i] > translation_order[j]) {
                uint32_t temp = translation_order[i];
                translation_order[i] = translation_order[j];
                translation_order[j] = temp;
            }
        }
    }

    // 5. Imprimir as strings usando a formatação cravada e na ordem correta
    for (uint32_t i = 0; i < num_translated; i++) {
        uint32_t pc = translation_order[i];
        uint32_t contagem_real = xmm_space[pc / 4];
        
        fprintf(outputF, fmt_inst, contagem_real, pc, translated_strings[pc]);
    }

    // Imprime o tracinho "-" do exit usando a mesma largura de espaços
    fprintf(outputF, fmt_exit, "-", PC_exit);
    
    fprintf(outputF, "\n");
    for (int i = 0; i < 15; i++){
        fprintf(outputF, "r%d=%08x,", i, ctx->uc_mcontext.gregs[REGS[i]]);
    }

    fprintf(outputF, "r15=%08x\n", ctx->uc_mcontext.gregs[REGS[15]]);

    fclose(inputF);
    fclose(outputF);

    munmap(PICO_MEM, 256);

    munmap(const_1_0_0_0, 16);
    munmap(const_0_1_0_0, 16);
    munmap(const_0_0_1_0, 16);
    munmap(const_0_0_0_1, 16);

    munmap(codeMemory.code, codeMemory.size);

    exit(0);
}

/* ═══════════════════════════════════════════════════════════════
   HELPERS DO main
   ═══════════════════════════════════════════════════════════════ */

// Faz mmap de 4 uint32_t num endereço fixo e inicializa os valores.
// Cada constante ocupa exatamente 16 bytes (um lane XMM de 128 bits),
// alinhada em página própria para endereçamento RIP-relativo seguro.
static uint32_t* create_const_vec(void* addr,
                                   uint32_t v0, uint32_t v1,
                                   uint32_t v2, uint32_t v3) {
    uint32_t* p = mmap(addr, 4 * 4,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                       -1, 0);
    p[0] = v0; p[1] = v1; p[2] = v2; p[3] = v3;
    return p;
}

// Aloca e registra a pilha alternativa usada pelos signal handlers.
// É necessária porque o SIGSEGV pode ser disparado com a pilha principal
// corrompida ou esgotada — a SA_ONSTACK garante que o handler survive.
static void setup_altstack(void) {
    stack_t ss;
    ss.ss_sp    = malloc(SIGSTKSZ);
    ss.ss_size  = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
}

// Registra um signal handler com sa_sigaction (recebe siginfo_t + ucontext).
// Centraliza o padrão repetido de zerar a struct + preencher + sigaction.
static void setup_signal(int signum,
                          void (*handler)(int, siginfo_t*, void*),
                          int flags) {
    struct sigaction sa = {0};
    sa.sa_sigaction = handler;
    sa.sa_flags     = flags;
    sigaction(signum, &sa, NULL);
}

// Lê o arquivo de entrada linha a linha e popula PICO_MEM.
// Cada linha tem 4 bytes em hex (ex: "0x00 0xA0 0xFF 0x01\n").
// O sscanf pula os dois chars "0x" com %*2c antes de ler cada byte.
// Retorna o número de bytes escritos (múltiplo de 4).
static uint32_t load_program(FILE* f) {
    uint32_t counter = 0;
    while (fgets(instructionBuffer, sizeof(instructionBuffer), f)) {
        sscanf(instructionBuffer,
               "%*2c%hhx %*2c%hhx %*2c%hhx %*2c%hhx\n",
               &PICO_MEM[counter],   &PICO_MEM[counter+1],
               &PICO_MEM[counter+2], &PICO_MEM[counter+3]);
        counter += 4;
    }
    return counter;
}

// Aloca o buffer JIT em RWX e o inicializa:
//   - Preenche tudo com 0xCC (INT3) para que toda instrução ainda não
//     traduzida dispare SIGTRAP na primeira execução (JIT lazy).
//   - Copia o bootstrap (initialCode) para o início, que zera os
//     registradores e pula para o offset 192 onde começam os slots.
// O tamanho total é: SIZE_INITIAL_CODE + (program_bytes * 4), pois
// cada byte de instrução PICO vira um slot de 16 bytes no JIT.
static void init_jit_memory(uint32_t program_bytes) {
    size_t total = SIZE_INITIAL_CODE + ((size_t)program_bytes << 2);
    codeMemory.code = mmap((void*)0x10007000, total,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                           -1, 0);
    codeMemory.size = total;
    memset(codeMemory.code,  0xCC, total);               // arma todas as armadilhas INT3
    memcpy(codeMemory.code, initialCode, SIZE_INITIAL_CODE); // instala o bootstrap
}

/* ═══════════════════════════════════════════════════════════════
   main

   Sequência de inicialização:
     1. Abre arquivos de I/O.
     2. Configura pilha alternativa e handlers de sinal.
     3. Mapeia PICO_MEM e os 4 vetores constante XMM em endereços fixos.
     4. Lê o programa do arquivo de entrada para PICO_MEM.
     5. Inicializa o buffer JIT com bootstrap + INT3s.
     6. Chama o buffer como função — o bootstrap roda, limpa os
        registradores e cai no primeiro INT3, disparando a magianegra.
   ═══════════════════════════════════════════════════════════════ */
int main (int argc, char* argv[]){

    inputF  = fopen(argv[1], "r");
    outputF = fopen(argv[2], "w");

    setup_altstack();
    // SIGTRAP = compilação JIT lazy (SA_RESTART: permite syscalls dentro do handler)
    setup_signal(SIGTRAP, magianegra,          SA_ONSTACK | SA_SIGINFO | SA_RESTART);
    // SIGSEGV = fim natural do programa (PC passou da última instrução mapeada)
    setup_signal(SIGSEGV, impressao_do_capeta, SA_ONSTACK | SA_SIGINFO);

    // Memória da máquina virtual: 256 bytes em endereço fixo (baixo, fácil de ver no gdb)
    PICO_MEM = (uint8_t*) mmap((void*)0x10000000, 256,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                               -1, 0);

    // Cada constante ocupa sua própria página para garantir isolamento e
    // permitir que o disp32 RIP-relativo resolva corretamente de qualquer
    // instrução no buffer JIT (que começa em 0x10007000).
    const_1_0_0_0 = create_const_vec((void*)0x10002000, 1, 0, 0, 0);
    const_0_1_0_0 = create_const_vec((void*)0x10003000, 0, 1, 0, 0);
    const_0_0_1_0 = create_const_vec((void*)0x10004000, 0, 0, 1, 0);
    const_0_0_0_1 = create_const_vec((void*)0x10005000, 0, 0, 0, 1);

    uint32_t program_bytes = load_program(inputF);
    init_jit_memory(program_bytes);

    // Dispara o bootstrap: zera registradores, inicializa FPU/XMM e salta
    // para o primeiro slot — que ainda é 0xCC, disparando a magianegra.
    void (*func)() = (void*) codeMemory.code;
    func();

    return 0;
}
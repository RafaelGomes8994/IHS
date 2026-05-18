#define _GNU_SOURCE

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>

#define SIZE_INITIAL_CODE 87

#define IF_REGISTERS(codeBothOld, codeReg0Old, codeReg1Old, codeBothNew) \
    do { \
        if (reg0Index < 8 && reg1Index < 8) { \
            codeBothOld \
        } else if (reg0Index < 8) { \
            codeReg0Old \
        } else if (reg1Index < 8) { \
            codeReg1Old \
        } else { \
            codeBothNew \
        } \
    } while (0)

#define IF_REGISTERS_MEM(codeBothOld, codeReg1Old, codeReg0Old, codeBothNew) \
    do { \
        if (reg0Index < 8 && reg1Index < 8) { \
            codeBothOld \
        } else if (reg1Index < 8) { \
            codeReg1Old \
        } else if (reg0Index < 8) { \
            codeReg0Old \
        } else { \
            codeBothNew \
        } \
    } while (0)

#define PRINT_BIN(x)                                             \
    do {                                                         \
        uint32_t __val = (uint32_t)(x);                          \
        for (int __i = 11; __i >= 0; __i--) {                    \
            putchar((__val & (1u << __i)) ? '1' : '0');          \
        }                                                        \
        putchar('\n');                                           \
    } while (0)

FILE* inputF;
FILE* outputF;

uint8_t REGS[16] = {
    REG_RAX,  // 0: rax
    REG_RCX,  // 1: rcx
    REG_RDX,  // 2: rdx
    REG_RBX,  // 3: rbx
    REG_RSP,  // 4: rsp
    REG_RBP,  // 5: rbp
    REG_RSI,  // 6: rsi
    REG_RDI,  // 7: rdi

    REG_R8,   // 8: r8
    REG_R9,   // 9: r9
    REG_R10,  // 10: r10
    REG_R11,  // 11: r11
    REG_R12,  // 12: r12
    REG_R13,  // 13: r13
    REG_R14,  // 14: r14
    REG_R15   // 15: r15
};
enum Flags {
    F_GREATER = 4,
    F_LESS = 2,
    F_EQUAL = 1
};

char instructionBuffer[30];
void* memTeste = NULL;

uint32_t PQP_REGS[16] = {0};
uint8_t* PICO_MEM = NULL;
uint32_t translation_order[256];
char translated_strings[256][64];
uint32_t num_translated = 0;

typedef struct {
    uint8_t* code;
    uint64_t pos;
    size_t size;
} Code_t;

Code_t codeMemory = {0};

uint8_t initialCode[SIZE_INITIAL_CODE] = {
    // xor rax, rax
    0x48, 0x31, 0xC0,
    // xor rbx, rbx
    0x48, 0x31, 0xDB,
    // xor rcx, rcx
    0x48, 0x31, 0xC9,
    // xor rdx, rdx
    0x48, 0x31, 0xD2,
    // xor rsi, rsi
    0x48, 0x31, 0xF6,
    // xor rdi, rdi
    0x48, 0x31, 0xFF,
    // xor rbp, rbp
    0x48, 0x31, 0xED,
    // xor rsp, rsp
    0x48, 0x31, 0xE4,
    // xor r8, r8
    0x4D, 0x31, 0xC0,
    // xor r9, r9
    0x4D, 0x31, 0xC9,
    // xor r10, r10
    0x4D, 0x31, 0xD2,
    // xor r11, r11
    0x4D, 0x31, 0xDB,
    // xor r12, r12
    0x4D, 0x31, 0xE4,
    // xor r13, r13
    0x4D, 0x31, 0xED,
    // xor r14, r14
    0x4D, 0x31, 0xF6,
    // xor r15, r15
    0x4D, 0x31, 0xFF,

    // Limpar estado FPU/SSE primeiro
    0x9B, 0xDB, 0xE2,  // fninit (limpa estado FPU)
    
    // pxor para todos os 16 registradores XMM
    0x66, 0x0F, 0xEF, 0xC0,  // pxor xmm0, xmm0
    0x66, 0x0F, 0xEF, 0xC9,  // pxor xmm1, xmm1  
    0x66, 0x0F, 0xEF, 0xD2,  // pxor xmm2, xmm2
    0x66, 0x0F, 0xEF, 0xDB,  // pxor xmm3, xmm3
    0x66, 0x44, 0x0F, 0xEF, 0xC0,  // pxor xmm8, xmm8
    0x66, 0x44, 0x0F, 0xEF, 0xC9,  // pxor xmm9, xmm9
    0x66, 0x44, 0x0F, 0xEF, 0xD2,  // pxor xmm10, xmm10
    0x66, 0x44, 0x0F, 0xEF, 0xDB   // pxor xmm11, xmm11
};




uint32_t* const_1_0_0_0;
uint32_t* const_0_1_0_0;
uint32_t* const_0_0_1_0;
uint32_t* const_0_0_0_1;

static inline void setApdCAddress(uint64_t addr){
    codeMemory.pos = addr;
}

static inline void apdC (uint8_t byte){
    codeMemory.code[codeMemory.pos++] = byte;
}

static inline void apdC_PTR32 (uint32_t ptr){

    uint32_t* asU32Code = (uint32_t*) &codeMemory.code[codeMemory.pos];
    *asU32Code = ptr;

    codeMemory.pos += 4;

}

static inline void insertPICO_MEM (uint8_t* ptr){

    uint32_t* asU32Code = (uint32_t*) &codeMemory.code[codeMemory.pos];
    *asU32Code = (uint32_t) ((uint64_t) ((uint32_t*) ptr));

    codeMemory.pos += 4;

}

static inline void apdC_InsertImm16 (int16_t imm16){
    uint32_t* asU32Code = (uint32_t*) &codeMemory.code[codeMemory.pos];
    int32_t imm32_signExtended = (int32_t) (imm16);
    *asU32Code = (uint32_t) imm32_signExtended;

    codeMemory.pos += 4;
}

static inline void apdC_InsertEnderecoJmp (int32_t imm32){
    uint32_t* asU32Code = (uint32_t*) &codeMemory.code[codeMemory.pos];
    *asU32Code = (uint32_t) imm32;

    codeMemory.pos += 4;
}


static inline void apdC_InsertCounter(uint32_t PC) {
    uint32_t inst_idx = PC / 4;
    uint32_t xmm_reg = inst_idx / 4;
    uint32_t slot_idx = inst_idx % 4;

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

    uint32_t* temp[4] = {const_1_0_0_0, const_0_1_0_0, const_0_0_1_0, const_0_0_0_1};
    
    // Distância exata da instrução até a constante na memória
    uint8_t* next_rip = codeMemory.code + codeMemory.pos + 4;
    int32_t disp32 = (int32_t)((uint8_t*)temp[slot_idx] - next_rip);
    apdC_PTR32((uint32_t)disp32);
}

void printCode(){

    // printf("\n[MEMORIA EXECUTAVEL] --------------------\n");
    for (int i = 0; i < (256 * 4); i++){
        // if ((i) % 16 == 0) printf("%02X:\t", i / 16);
        // printf("%02X ", codeMemory.code[i + SIZE_INITIAL_CODE]);
        // if ((i+1) % 16 == 0) printf("\n");
    }

    // printf("-----------------------------------------\n");

}

void printRegs(ucontext_t* ctx){
    // printf("\nX86 REG CONTEXT -------------------------------------\n");
    // printf("REG RIP: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_RIP]);
    // printf("REG EFL: 0b"); PRINT_BIN(ctx->uc_mcontext.gregs[REG_EFL]); printf("\n");
    // printf("REG RAX: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_RAX]);
    // printf("REG RCX: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_RCX]);
    // printf("REG RDX: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_RDX]);
    // printf("REG RBX: 0x%08lX\n", ctx->uc_mcontext.gregs[REG_RBX]);
    // printf("REG RSP: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_RSP]);
    // printf("REG RBP: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_RBP]);
    // printf("REG RSI: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_RSI]);
    // printf("REG RDI: 0x%08lX\n", ctx->uc_mcontext.gregs[REG_RDI]);
    // printf("REG  R8: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_R8]);
    // printf("REG  R9: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_R9]);
    // printf("REG R10: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_R10]);
    // printf("REG R11: 0x%08lX\n", ctx->uc_mcontext.gregs[REG_R11]);
    // printf("REG R12: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_R12]);
    // printf("REG R13: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_R13]);
    // printf("REG R14: 0x%08lX\t", ctx->uc_mcontext.gregs[REG_R14]);
    // printf("REG R15: 0x%08lX\n", ctx->uc_mcontext.gregs[REG_R15]);
    // printf("-----------------------------------------------------\n");

    struct _libc_fpstate* fp = ctx->uc_mcontext.fpregs;
    if (!fp) {
        // printf("Sem contexto FPU (fpregs = NULL)\n");
        return;
    }

    // Verificar se o contexto FPU está válido
    // printf("FPU Status: cwd=0x%04X swd=0x%04X\n", fp->cwd, fp->swd);
    
    // Mostrar todos os 16 registradores XMM disponíveis
    for (int i = 0; i < 16; i++) {
        const uint32_t* words = (const uint32_t*)&fp->_xmm[i];
        // printf("XMM%-2d: [%d, %d, %d, %d]\n", 
        //    i, words[0], words[1], words[2], words[3]);
    }
    // printf("-----------------------------------------------------\n");
}

void fallback (int32_t sig, siginfo_t *si, void* context){
    
    ucontext_t* ctx = (ucontext_t*) context;
    
    uint64_t rip = (uint64_t) ctx->uc_mcontext.gregs[REG_RIP];
    uint32_t instCount = ((rip) - (uint64_t) codeMemory.code - SIZE_INITIAL_CODE) >> 4; 
    uint32_t PC = instCount << 2;
    uint8_t opcode = PICO_MEM[PC];


    translation_order[num_translated++] = PC;

    setApdCAddress((rip) - 1 -  (uint64_t) codeMemory.code);
    
    // printf("\n[fallback chamado!]\n\n");

    // printf("contexto pqp ----------------------------------------\n");
    // printf("line: %04u\t pc: %04x\t opcode: %02x\t", PC / 4 + 1, PC, opcode);
    // printf("\ndeslocamento: %llu\n", codeMemory.pos);
    // printf("-----------------------------------------------------\n");    

    apdC_InsertCounter(PC); //7 bytes
    switch (opcode) {
        case 0x00:
            {
                uint8_t reg0Index = PICO_MEM[PC+1]>>4;
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);

                sprintf(translated_strings[PC], "mov_r%u=%08x\n", reg0Index, (int32_t)immValue);
                // printf("[instrucao] \t mov_r%u=%08x\n", reg0Index, (int32_t) immValue);

                if (reg0Index <= 7){
                    apdC(0xb8 + reg0Index); apdC_InsertImm16(immValue);
                    apdC(0x90); apdC(0x90); 
                }
                else{
                    apdC(0x41); apdC(0xb8 + reg0Index - 8); apdC_InsertImm16(immValue);
                    apdC(0x90); 
                }
                
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
                // printf("codigo vai continuar em: %02x com pos: %ld\n", *((uint8_t*) ctx->uc_mcontext.gregs[REG_RIP]), codeMemory.pos - SIZE_INITIAL_CODE);
            }
            break;

        case 0x01:
            {
                uint8_t reg0Index = PICO_MEM[PC+1] >> 4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("[instrucao] \t mov_r%u=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "mov_r%u=r%u\n", reg0Index, reg1Index);

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
        
        case 0x02:
            {
                uint8_t reg0Index = PICO_MEM[PC+1]>>4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("[instrucao] \t mov_r%u=[r%u]\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "mov_r%u=[r%u]\n", reg0Index, reg1Index);

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
        
        case 0x03:
            {
                uint8_t reg0Index = PICO_MEM[PC+1]>>4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("[instrucao] \t mov_[r%u]=r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "mov_[r%u]=r%u\n", reg0Index, reg1Index);

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

        case 0x04:
            {
                uint8_t reg0Index = PICO_MEM[PC+1]>>4;
                uint8_t reg1Index = PICO_MEM[PC+1] & 0xF;

                // printf("[instrucao] \t cmp_r%u<=>r%u\n", reg0Index, reg1Index);
                sprintf(translated_strings[PC], "cmp_r%u<=>r%u\n", reg0Index, reg1Index);

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

        case 0x05:
            {   
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);
                uint32_t PC_target = (PC + immValue + 4) & 0xFFFF;

                apdC(0xE9); 

                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + ((PC_target / 4) * 16);
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
        
        case 0x06:
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);
                uint32_t PC_target = (PC + immValue + 4) & 0xFFFF;
                
                apdC(0x0F); apdC(0x8F);

                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + ((PC_target / 4) * 16);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90); 

                // printf( "[instrucao] \t jg_%04x\n", PC_target);
                sprintf(translated_strings[PC], "jg_%04x\n", PC_target);

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x07:
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);
                uint32_t PC_target = (PC + immValue + 4) & 0xFFFF;
                
                apdC(0x0F); apdC(0x8C);

                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + ((PC_target / 4) * 16);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90); 

                // printf("[instrucao] \t jl_%04x\n", PC_target);
                sprintf(translated_strings[PC], "jl_%04x\n", PC_target);

                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;

        case 0x08:
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[PC+2]);
                uint32_t PC_target = (PC + immValue + 4) & 0xFFFF;
                
                apdC(0x0F); apdC(0x84);

                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + ((PC_target / 4) * 16);
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

    // Estas funções presumivelmente imprimem no terminal
    printRegs(ctx);
    printCode();

    // printf("\n");
}


void end (int32_t sig, siginfo_t *si, void* context){
    ucontext_t* ctx = (ucontext_t*) context;
    //printCode();

    uint64_t rip = (uint64_t) ctx->uc_mcontext.gregs[REG_RIP];
    uint32_t instCount = ((rip) - (uint64_t) codeMemory.code - SIZE_INITIAL_CODE) >> 4; 

    // printf("rip: %04X\n", rip);
    // printf("Instcount: %04X\n", instCount);
    uint32_t PC_exit = instCount << 2;
    // printf("Info: %d\n", si->si_addr);

    // Busca os contadores direto do silício (XMM Registers)
    uint8_t *fp = (uint8_t *)ctx->uc_mcontext.fpregs;
    uint32_t *xmm_space = (uint32_t *)(fp + 160);

    for (uint32_t i = 0; i < num_translated; i++) {
        uint32_t pc = translation_order[i];
        uint32_t contagem_real = xmm_space[pc / 4];
        fprintf(outputF, "%u:%04x:%s", contagem_real, pc, translated_strings[pc]);
    }

    fprintf(outputF, "-:%04x:exit\n", PC_exit );
    printRegs(ctx);


    

    fprintf(outputF, "\n");
    for (int i = 0; i < 15; i++){
        fprintf(outputF, "r%d=%08X,", i, ctx->uc_mcontext.gregs[REGS[i]]);
    }

    fprintf(outputF, "r15=%08X\n", ctx->uc_mcontext.gregs[REGS[15]]);

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

int main (int argc, char* argv[]){

    

    inputF = fopen(argv[1], "r");
    outputF = fopen(argv[2], "w");

    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);

    struct sigaction sa_jit = {0};
    sa_jit.sa_sigaction = fallback;
    sa_jit.sa_flags = SA_ONSTACK | SA_SIGINFO | SA_RESTART;
    sigaction(SIGTRAP, &sa_jit, NULL);

    struct sigaction sa_end = {0};
    sa_end.sa_sigaction = end;
    sa_end.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigaction(SIGSEGV, &sa_end, NULL);

    void* targetAddr0 = (void*)(0x10000000);
    void* targetAddr1 = (void*)(0x10001000);
    void* targetAddr2 = (void*)(0x10002000);
    void* targetAddr3 = (void*)(0x10003000);
    void* targetAddr4 = (void*)(0x10004000);
    void* targetAddr5 = (void*)(0x10005000);
    PICO_MEM = (uint8_t*) mmap(targetAddr0, 256, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    const_1_0_0_0 = (uint32_t*) mmap(targetAddr2, 4*4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    const_0_1_0_0 = (uint32_t*) mmap(targetAddr3, 4*4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    const_0_0_1_0 = (uint32_t*) mmap(targetAddr4, 4*4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    const_0_0_0_1 = (uint32_t*) mmap(targetAddr5, 4*4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    
    const_1_0_0_0[0] = 1;
    const_1_0_0_0[1] = 0;
    const_1_0_0_0[2] = 0;
    const_1_0_0_0[3] = 0;


    const_0_1_0_0[0] = 0;
    const_0_1_0_0[1] = 1;
    const_0_1_0_0[2] = 0;
    const_0_1_0_0[3] = 0;

    const_0_0_1_0[0] = 0;
    const_0_0_1_0[1] = 0;
    const_0_0_1_0[2] = 1;
    const_0_0_1_0[3] = 0;


    const_0_0_0_1[0] = 0;
    const_0_0_0_1[1] = 0;
    const_0_0_0_1[2] = 0;
    const_0_0_0_1[3] = 1;

    

    

    uint32_t counter = 0;
    while (fgets(instructionBuffer, 30, inputF)){
        
        sscanf(instructionBuffer, "%*2c%hhx %*2c%hhx %*2c%hhx %*2c%hhx\n", &PICO_MEM[counter], &PICO_MEM[counter+1], &PICO_MEM[counter+2], &PICO_MEM[counter+3]);

        counter += 4;

    }

// Substitua o seu mmap atual por este bloco vizinho:
    void* targetAddrJIT = (void*)(0x10007000);
    codeMemory.code = (uint8_t*) mmap(targetAddrJIT, SIZE_INITIAL_CODE+(counter<<2), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    
    memset(codeMemory.code, 0xCC, SIZE_INITIAL_CODE+(counter<<2));

    codeMemory.size = SIZE_INITIAL_CODE+(counter<<2);

    

    memcpy(codeMemory.code, initialCode, SIZE_INITIAL_CODE);


    void (*func)() = (void*) codeMemory.code;

    
    func();

    
    return 0;
}

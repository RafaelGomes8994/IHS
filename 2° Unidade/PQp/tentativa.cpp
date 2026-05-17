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

FILE* inputF;
FILE* outputF;

uint8_t REGS[16] = {
    REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
    REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15
};

uint8_t* PICO_MEM = NULL;
uint8_t ORIGINAL_PICO_MEM[256] = {0}; 

typedef struct {
    uint8_t* code;
    uint64_t pos;
    size_t size;
} Code_t;

Code_t codeMemory = {0};

uint8_t initialCode[SIZE_INITIAL_CODE] = {
    0x48, 0x31, 0xC0, 0x48, 0x31, 0xDB, 0x48, 0x31, 0xC9, 0x48, 0x31, 0xD2,
    0x48, 0x31, 0xF6, 0x48, 0x31, 0xFF, 0x48, 0x31, 0xED, 0x48, 0x31, 0xE4,
    0x4D, 0x31, 0xC0, 0x4D, 0x31, 0xC9, 0x4D, 0x31, 0xD2, 0x4D, 0x31, 0xDB,
    0x4D, 0x31, 0xE4, 0x4D, 0x31, 0xED, 0x4D, 0x31, 0xF6, 0x4D, 0x31, 0xFF,
    0x9B, 0xDB, 0xE2, 
    0x66, 0x0F, 0xEF, 0xC0, 0x66, 0x0F, 0xEF, 0xC9, 0x66, 0x0F, 0xEF, 0xD2, 0x66, 0x0F, 0xEF, 0xDB,
    0x66, 0x44, 0x0F, 0xEF, 0xC0, 0x66, 0x44, 0x0F, 0xEF, 0xC9, 0x66, 0x44, 0x0F, 0xEF, 0xD2, 0x66, 0x44, 0x0F, 0xEF, 0xDB
};

uint32_t* const_1_0_0_0;
uint32_t* const_0_1_0_0;
uint32_t* const_0_0_1_0;
uint32_t* const_0_0_0_1;

// Nao sei como isso funciona por dentro, mas funciona, entt que si foda;
static inline void moveCodeCursorNaMagiaNegra(uint64_t addr){
    codeMemory.pos = addr;
}

// Joga um byte na panela e reza para sair opcode.
static inline void jogaByteNaMagiaNegra(uint8_t byte){
    codeMemory.code[codeMemory.pos++] = byte;
}

// Enfia um u32 inteiro na poção, sem perguntar demais.
static inline void enfiaU32NaMagiaNegra(uint32_t ptr){
    uint32_t* asU32Code = (uint32_t*) &codeMemory.code[codeMemory.pos];
    *asU32Code = ptr;
    codeMemory.pos += 4;
}

// Esse aqui escreve endereco bruto; parece feio porque realmente eh.
static inline void enfiaPonteiroDoPicoNaMagiaNegra(uint8_t* ptr){
    uint32_t* asU32Code = (uint32_t*) &codeMemory.code[codeMemory.pos];
    *asU32Code = (uint32_t) ((uint64_t) ((uint32_t*) ptr));
    codeMemory.pos += 4;
}

// Imediato sign-extended, porque o caos tambem precisa de carimbo.
static inline void enfiaImm16NaMagiaNegra(int16_t imm16){
    uint32_t* asU32Code = (uint32_t*) &codeMemory.code[codeMemory.pos];
    int32_t imm32_signExtended = (int32_t) (imm16);
    *asU32Code = (uint32_t) imm32_signExtended;
    codeMemory.pos += 4;
}

// Deslocamento relativo do jump: feitiço de teletransporte com offset.
static inline void enfiaJmpRel32NaMagiaNegra(int32_t imm32){
    uint32_t* asU32Code = (uint32_t*) &codeMemory.code[codeMemory.pos];
    *asU32Code = (uint32_t) imm32;
    codeMemory.pos += 4;
}

// Conta cada instrucao doida e guarda no XMM, porque por que nao?
static inline void carimbaContadorNaMagiaNegra(uint32_t PC){
    uint8_t compact_idx = (PC == 0) ? 0 : (PC - 20) / 4 + 1;
    uint8_t xmm_reg = compact_idx / 4;
    uint8_t lane = compact_idx % 4;

    jogaByteNaMagiaNegra(0x66); jogaByteNaMagiaNegra(0x0F); jogaByteNaMagiaNegra(0xFE);
    jogaByteNaMagiaNegra(0x04 + (xmm_reg << 3)); jogaByteNaMagiaNegra(0x25);

    uint32_t* temp[4] = {const_1_0_0_0, const_0_1_0_0, const_0_0_1_0, const_0_0_0_1};
    enfiaU32NaMagiaNegra((uint32_t) (uint64_t) temp[lane]);
}

#define setApdCAddress moveCodeCursorNaMagiaNegra
#define apdC jogaByteNaMagiaNegra
#define apdC_PTR32 enfiaU32NaMagiaNegra
#define insertPICO_MEM enfiaPonteiroDoPicoNaMagiaNegra
#define apdC_InsertImm16 enfiaImm16NaMagiaNegra
#define apdC_InsertEnderecoJmp enfiaJmpRel32NaMagiaNegra
#define apdC_InsertCounter carimbaContadorNaMagiaNegra

void trapHandler (int32_t sig, siginfo_t *si, void* context){
    ucontext_t* ctx = (ucontext_t*) context;
    uint64_t currentRip = (uint64_t) ctx->uc_mcontext.gregs[REG_RIP];
    uint32_t instructionIndex = ((currentRip) - (uint64_t) codeMemory.code - SIZE_INITIAL_CODE) >> 4; 
    uint32_t pc = instructionIndex << 2;
    uint8_t opcodeByte = PICO_MEM[pc];

    setApdCAddress((currentRip) - 1 -  (uint64_t) codeMemory.code);
    apdC_InsertCounter(pc); 

    switch (opcodeByte) {
        case 0x00:
            {
                uint8_t reg0Index = PICO_MEM[pc+1]>>4;
                int16_t immValue = *((int16_t*) &PICO_MEM[pc+2]);

                if (reg0Index <= 7){
                    apdC(0xb8 + reg0Index); apdC_InsertImm16(immValue);
                    apdC(0x90); apdC(0x90);
                } else {
                    apdC(0x41); apdC(0xb8 + reg0Index - 8); apdC_InsertImm16(immValue);
                    apdC(0x90);
                }
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x01:
            {
                uint8_t reg0Index = PICO_MEM[pc+1] >> 4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS(
                    apdC(0x89); apdC(0xC0 + reg0Index + (reg1Index << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x44); apdC(0x89); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x41); apdC(0x89); apdC(0xC0 + reg0Index - 8 + (reg1Index << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x45); apdC(0x89); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x02:
            {
                uint8_t reg0Index = PICO_MEM[pc+1]>>4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS_MEM(
                    apdC(0x8B); apdC(0x80 + reg1Index + (reg0Index << 3)); insertPICO_MEM(PICO_MEM); apdC(0x90); ,
                    apdC(0x44); apdC(0x8B); apdC(0x80 + reg1Index + ((reg0Index - 8) << 3)); insertPICO_MEM(PICO_MEM); ,
                    apdC(0x41); apdC(0x8B); apdC(0x80 + reg1Index - 8 + (reg0Index << 3)); insertPICO_MEM(PICO_MEM); ,
                    apdC(0x45); apdC(0x8B); apdC(0x80 + reg1Index - 8 + ((reg0Index - 8) << 3)); insertPICO_MEM(PICO_MEM);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x03:
            {
                uint8_t reg0Index = PICO_MEM[pc+1]>>4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS(
                    apdC(0x89); apdC(0x80 + reg0Index + (reg1Index << 3)); insertPICO_MEM(PICO_MEM); apdC(0x90); ,
                    apdC(0x44); apdC(0x89); apdC(0x80 + reg0Index + ((reg1Index - 8) << 3)); insertPICO_MEM(PICO_MEM); ,
                    apdC(0x41); apdC(0x89); apdC(0x80 + reg0Index - 8 + (reg1Index << 3)); insertPICO_MEM(PICO_MEM); ,
                    apdC(0x45); apdC(0x89); apdC(0x80 + reg0Index - 8 + ((reg1Index - 8) << 3)); insertPICO_MEM(PICO_MEM);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x04:
            {
                uint8_t reg0Index = PICO_MEM[pc+1]>>4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS(
                    apdC(0x39); apdC(0xC0 + reg0Index + (reg1Index << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x44); apdC(0x39); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x41); apdC(0x39); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x45); apdC(0x39); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x05:
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[pc+2]);
                uint32_t targetPc = (pc + immValue + 4);
                apdC(0xE9);
                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + ((targetPc / 4) * 16);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90); apdC(0x90);
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x06:
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[pc+2]);
                uint32_t targetPc = (pc + immValue + 4);
                apdC(0x0F); apdC(0x8F);
                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + ((targetPc / 4) * 16);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90);
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x07:
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[pc+2]);
                uint32_t targetPc = (pc + immValue + 4);
                apdC(0x0F); apdC(0x8C);
                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + ((targetPc / 4) * 16);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90);
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x08:
            {
                int16_t immValue = *((int16_t*) &PICO_MEM[pc+2]);
                uint32_t targetPc = (pc + immValue + 4);
                apdC(0x0F); apdC(0x84);
                uint8_t * nextRip   = codeMemory.code + (codeMemory.pos + 4);
                uint8_t * destRip   = codeMemory.code + SIZE_INITIAL_CODE + ((targetPc / 4) * 16);
                int32_t   rel32     = (int32_t)(destRip - nextRip);
                apdC_InsertEnderecoJmp(rel32);
                apdC(0x90);
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x09:
            {
                uint8_t reg0Index = PICO_MEM[pc+1] >> 4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS(
                    apdC(0x01); apdC(0xC0 + reg0Index + (reg1Index << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x44); apdC(0x01); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x41); apdC(0x01); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x45); apdC(0x01); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break; 
        case 0X0A:
            {
                uint8_t reg0Index = PICO_MEM[pc+1] >> 4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS(
                    apdC(0x29); apdC(0xC0 + reg0Index + (reg1Index << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x44); apdC(0x29); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x41); apdC(0x29); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x45); apdC(0x29); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x0B:
            {
                uint8_t reg0Index = PICO_MEM[pc+1] >> 4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS(
                    apdC(0x21); apdC(0xC0 + reg0Index + (reg1Index << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x44); apdC(0x21); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x41); apdC(0x21); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x45); apdC(0x21); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x0C:
            {
                uint8_t reg0Index = PICO_MEM[pc+1] >> 4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS(
                    apdC(0x09); apdC(0xC0 + reg0Index + (reg1Index << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x44); apdC(0x09); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x41); apdC(0x09); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x45); apdC(0x09); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x0D:
            {
                uint8_t reg0Index = PICO_MEM[pc+1] >> 4;
                uint8_t reg1Index = PICO_MEM[pc+1] & 0xF;
                IF_REGISTERS(
                    apdC(0x31); apdC(0xC0 + reg0Index + (reg1Index << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x44); apdC(0x31); apdC(0xC0 + reg0Index + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x41); apdC(0x31); apdC(0xC0 + reg0Index - 8 + (reg1Index<< 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90); ,
                    apdC(0x45); apdC(0x31); apdC(0xC0 + reg0Index - 8 + ((reg1Index - 8) << 3)); apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                );
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x0E:
            {
                uint8_t reg0Index = PICO_MEM[pc+1] >> 4;
                uint8_t slImm = PICO_MEM[pc+3] & 0xF;
                if (reg0Index < 8){
                    apdC(0xC1); apdC(0xE0 + reg0Index); apdC(slImm);
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                } else {
                    apdC(0x41); apdC(0xC1); apdC(0xE0 + reg0Index - 8); apdC(slImm);
                    apdC(0x90); apdC(0x90); apdC(0x90);
                }
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
        case 0x0F:
            {
                uint8_t reg0Index = PICO_MEM[pc+1] >> 4;
                uint8_t slImm = PICO_MEM[pc+3] & 0xF;
                if (reg0Index < 8){
                    apdC(0xC1); apdC(0xF8 + reg0Index); apdC(slImm);
                    apdC(0x90); apdC(0x90); apdC(0x90); apdC(0x90);
                } else {
                    apdC(0x41); apdC(0xC1); apdC(0xF8 + reg0Index - 8); apdC(slImm);
                    apdC(0x90); apdC(0x90); apdC(0x90);
                }
                ctx->uc_mcontext.gregs[REG_RIP] = (uint64_t) &codeMemory.code[codeMemory.pos - 16];
            }
            break;
    }
}

void segvHandler (int32_t sig, siginfo_t *si, void* context){
    ucontext_t* ctx = (ucontext_t*) context;
    struct _libc_fpstate* fpState = (struct _libc_fpstate*) ctx->uc_mcontext.fpregs;

    for (int i = 0; i <= 140; i += 4) {
        if (i > 0 && i < 20) continue; 
        
        uint8_t compact_idx = (i == 0) ? 0 : (i - 20) / 4 + 1;
        uint8_t xmm_reg = compact_idx / 4;
        uint8_t lane = compact_idx % 4;
        
        uint32_t count = 0;
        if (fpState) {
            count = ((const uint32_t*)&fpState->_xmm[xmm_reg])[lane];
        }

        if (count > 0) {
            uint8_t opcode = ORIGINAL_PICO_MEM[i];
            uint8_t rx = ORIGINAL_PICO_MEM[i+1] >> 4;
            uint8_t ry = ORIGINAL_PICO_MEM[i+1] & 0xF;
            int16_t imm = *((int16_t*)&ORIGINAL_PICO_MEM[i+2]);
            uint32_t target = (i + 4 + imm) & 0xFFFF;

            fprintf(outputF, "%u:%04x:", count, i);

            switch (opcode) {
                case 0x00: fprintf(outputF, "mov_r%u=%08x\n", rx, (int32_t)imm); break;
                case 0x01: fprintf(outputF, "mov_r%u=r%u\n", rx, ry); break;
                case 0x02: fprintf(outputF, "mov_r%u=[r%u]\n", rx, ry); break;
                case 0x03: fprintf(outputF, "mov_[r%u]=r%u\n", rx, ry); break;
                case 0x04: fprintf(outputF, "cmp_r%u<=>r%u\n", rx, ry); break;
                case 0x05: 
                    if (target == 0xDEAD) fprintf(outputF, "jmp_dead\n");
                    else fprintf(outputF, "jmp_%04x\n", target);
                    break;
                case 0x06: fprintf(outputF, "jg_%04x\n", target); break;
                case 0x07: fprintf(outputF, "jl_%04x\n", target); break;
                case 0x08: fprintf(outputF, "je_%04x\n", target); break;
                case 0x09: fprintf(outputF, "add_r%u+=r%u\n", rx, ry); break;
                case 0x0A: fprintf(outputF, "sub_r%u-=r%u\n", rx, ry); break;
                case 0x0B: fprintf(outputF, "and_r%u&=r%u\n", rx, ry); break;
                case 0x0C: fprintf(outputF, "or_r%u|=r%u\n", rx, ry); break;
                case 0x0D: fprintf(outputF, "xor_r%u^=r%u\n", rx, ry); break;
                case 0x0E: fprintf(outputF, "sal_r%u<<=%u\n", rx, imm & 0x1F); break;
                case 0x0F: fprintf(outputF, "sar_r%u>>=%u\n", rx, imm & 0x1F); break;
            }
        }
    }

    fprintf(outputF, "-:dead:exit\n\n");
    fprintf(outputF, "r0=%08x,r1=%08x,r2=%08x,r3=%08x,r4=%08x,r5=%08x,r6=%08x,r7=%08x,r8=%08x,r9=%08x,r10=%08x,r11=%08x,r12=%08x,r13=%08x,r14=%08x,r15=%08x\n",
            (uint32_t)ctx->uc_mcontext.gregs[REGS[0]], (uint32_t)ctx->uc_mcontext.gregs[REGS[1]],
            (uint32_t)ctx->uc_mcontext.gregs[REGS[2]], (uint32_t)ctx->uc_mcontext.gregs[REGS[3]],
            (uint32_t)ctx->uc_mcontext.gregs[REGS[4]], (uint32_t)ctx->uc_mcontext.gregs[REGS[5]],
            (uint32_t)ctx->uc_mcontext.gregs[REGS[6]], (uint32_t)ctx->uc_mcontext.gregs[REGS[7]],
            (uint32_t)ctx->uc_mcontext.gregs[REGS[8]], (uint32_t)ctx->uc_mcontext.gregs[REGS[9]],
            (uint32_t)ctx->uc_mcontext.gregs[REGS[10]], (uint32_t)ctx->uc_mcontext.gregs[REGS[11]],
            (uint32_t)ctx->uc_mcontext.gregs[REGS[12]], (uint32_t)ctx->uc_mcontext.gregs[REGS[13]],
            (uint32_t)ctx->uc_mcontext.gregs[REGS[14]], (uint32_t)ctx->uc_mcontext.gregs[REGS[15]]
    );

    if (inputF) fclose(inputF);
    if (outputF) fclose(outputF);

    munmap(PICO_MEM, 256);
    munmap(const_1_0_0_0, 16);
    munmap(const_0_1_0_0, 16);
    munmap(const_0_0_1_0, 16);
    munmap(const_0_0_0_1, 16);
    if (codeMemory.code) munmap(codeMemory.code, codeMemory.size);

    exit(0);
}

int main (int argc, char* argv[]){
    if (argc < 3) {
        printf("Uso: %s <input> <output>\n", argv[0]);
        return 1;
    }

    inputF = fopen(argv[1], "r");
    if (!inputF) {
        printf("Erro ao abrir %s\n", argv[1]);
        return 1;
    }
    outputF = fopen(argv[2], "w");
    if (!outputF) {
        printf("Erro ao criar %s\n", argv[2]);
        fclose(inputF);
        return 1;
    }

    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);

    struct sigaction sa_jit = {0};
    sa_jit.sa_sigaction = trapHandler;
    sa_jit.sa_flags = SA_ONSTACK | SA_SIGINFO | SA_RESTART;
    sigaction(SIGTRAP, &sa_jit, NULL);

    struct sigaction sa_end = {0};
    sa_end.sa_sigaction = segvHandler;
    sa_end.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigaction(SIGSEGV, &sa_end, NULL);

    void* targetAddr0 = (void*)(0x10000000);
    void* targetAddr2 = (void*)(0x10002000);
    void* targetAddr3 = (void*)(0x10003000);
    void* targetAddr4 = (void*)(0x10004000);
    void* targetAddr5 = (void*)(0x10005000);
    
    PICO_MEM = (uint8_t*) mmap(targetAddr0, 256, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    const_1_0_0_0 = (uint32_t*) mmap(targetAddr2, 4*4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    const_0_1_0_0 = (uint32_t*) mmap(targetAddr3, 4*4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    const_0_0_1_0 = (uint32_t*) mmap(targetAddr4, 4*4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    const_0_0_0_1 = (uint32_t*) mmap(targetAddr5, 4*4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    const_1_0_0_0[0] = 1; const_1_0_0_0[1] = 0; const_1_0_0_0[2] = 0; const_1_0_0_0[3] = 0;
    const_0_1_0_0[0] = 0; const_0_1_0_0[1] = 1; const_0_1_0_0[2] = 0; const_0_1_0_0[3] = 0;
    const_0_0_1_0[0] = 0; const_0_0_1_0[1] = 0; const_0_0_1_0[2] = 1; const_0_0_1_0[3] = 0;
    const_0_0_0_1[0] = 0; const_0_0_0_1[1] = 0; const_0_0_0_1[2] = 0; const_0_0_0_1[3] = 1;

    uint32_t counter = 0;
    char instructionBuffer[30];
    while (fgets(instructionBuffer, 30, inputF)){
        sscanf(instructionBuffer, "%*2c%hhx %*2c%hhx %*2c%hhx %*2c%hhx\n", 
               &PICO_MEM[counter], &PICO_MEM[counter+1], &PICO_MEM[counter+2], &PICO_MEM[counter+3]);
        
        // Cópia Segura
        ORIGINAL_PICO_MEM[counter]   = PICO_MEM[counter];
        ORIGINAL_PICO_MEM[counter+1] = PICO_MEM[counter+1];
        ORIGINAL_PICO_MEM[counter+2] = PICO_MEM[counter+2];
        ORIGINAL_PICO_MEM[counter+3] = PICO_MEM[counter+3];

        counter += 4;
    }

    codeMemory.code = (uint8_t*) mmap(NULL, SIZE_INITIAL_CODE+(counter<<2), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(codeMemory.code, 0xCC, SIZE_INITIAL_CODE+(counter<<2));
    codeMemory.size = SIZE_INITIAL_CODE+(counter<<2);

    memcpy(codeMemory.code, initialCode, SIZE_INITIAL_CODE);

    void (*func)() = (void (*)()) codeMemory.code;
    func();
    
    return 0;
}
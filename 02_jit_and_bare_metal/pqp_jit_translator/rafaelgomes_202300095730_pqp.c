#define _GNU_SOURCE

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>

/* Flag para forçar a alocação de forma relativa segura (deslocamento 32-bit) */
#ifndef MAP_32BIT
#define MAP_32BIT 0x40
#endif

/* ═══════════════════════════════════════════════════════════════
   GEOMETRIA OTIMIZADA DO JIT E DA MÁQUINA VIRTUAL
   ═══════════════════════════════════════════════════════════════ */
#define BOOTSTRAP_SIZE 128
#define SLOT_BYTES     32
#define PQP_MEM_SIZE   256  /* Limite restrito de Von Neumann para forçar EXIT */
#define MAX_INST       0x10000

/* Mapeamento de registradores da arquitetura PQP para a arquitetura x86_64 */
static const uint8_t PQP_TO_X86[16] = {
    0,  1,  2,  3,  4,  5,  6,  7, 
    8,  9,  10, 11, 12, 13, 14, 15 
};

/* Mapeamento dos registradores de contexto para a saída nativa */
static const uint8_t PQP_TO_CONTEXT[16] = {
    REG_RAX, REG_RCX, REG_RDX, REG_RBX,
    REG_RSP, REG_RBP, REG_RSI, REG_RDI,
    REG_R8,  REG_R9,  REG_R10, REG_R11,
    REG_R12, REG_R13, REG_R14, REG_R15
};

/* Globais para leitura e escrita */
FILE *g_input;
FILE *g_output;

uint8_t  *PICO_MEMO = NULL;
uint32_t *vector_lanes = NULL; /* Constantes de incremento do Profiler (XMM) */

typedef struct { uint8_t *code; uint64_t pos; size_t size; } JitBuf;
static JitBuf jit;

static uint32_t  xlat_order[MAX_INST];
static uint32_t  xlat_count;
static char      xlat_str[MAX_INST][64];
static char      g_line_buf[64];

/* ═══════════════════════════════════════════════════════════════
   EMISSORES BÁSICOS
   ═══════════════════════════════════════════════════════════════ */
static inline void reposicionar_jit(uint64_t novo_offset) { jit.pos = novo_offset; }
static inline void emitir_byte(uint8_t byte_value)       { jit.code[jit.pos++] = byte_value; }
static inline void emitir_u32(uint32_t value_u32)        { *(uint32_t *)&jit.code[jit.pos] = value_u32; jit.pos += 4; }
static inline void emitir_i32_sx(int16_t signed_imm16)   { *(uint32_t *)&jit.code[jit.pos] = (uint32_t)(int32_t)signed_imm16; jit.pos += 4; }
static inline void emitir_i32_rel(int32_t rel_offset)    { *(int32_t *)&jit.code[jit.pos] = rel_offset; jit.pos += 4; }

static inline uint8_t codificar_reg_x86(uint8_t indice_reg_pqp) { return PQP_TO_X86[indice_reg_pqp] & 7; }
static inline int precisa_rex(uint8_t indice_reg_pqp)           { return PQP_TO_X86[indice_reg_pqp] >= 8; }

/* Injetor de Profiler Massivo via Registradores Vetoriais  */
static inline void emit_fast_profiler(uint32_t program_counter) {
    uint32_t instruction_id = program_counter / 4;
    uint32_t vector_register_index = instruction_id / 4;
    uint32_t lane_index = instruction_id % 4;

    if (vector_register_index >= 8) {
        emitir_byte(0x66); emitir_byte(0x44); emitir_byte(0x0F); emitir_byte(0xFE);
        emitir_byte(0x05 | ((vector_register_index - 8) << 3));
    } else {
        emitir_byte(0x66); emitir_byte(0x0F); emitir_byte(0xFE);
        emitir_byte(0x05 | (vector_register_index << 3));
    }

    uint8_t *next_rip = jit.code + jit.pos + 4;
    uint8_t *destination_memory = (uint8_t *)&vector_lanes[lane_index * 4];
    emitir_u32((uint32_t)(destination_memory - next_rip));
}

/* ═══════════════════════════════════════════════════════════════
   GERAÇÃO DO BOOTSTRAP DINÂMICO
   ═══════════════════════════════════════════════════════════════ */
static void build_bootstrap() {
    reposicionar_jit(0); 
    
    /* 1. Zera GPRs nativos */
    for (int i = 15; i >= 0; i--) {
        uint8_t enc = codificar_reg_x86(i);
        if (precisa_rex(i)) emitir_byte(0x4D); else emitir_byte(0x48);
        emitir_byte(0x29);
        emitir_byte(0xC0 | (enc << 3) | enc);
    }
    
    /* 2. Reseta o Floating Point Unit nativo */
    emitir_byte(0xDB); emitir_byte(0xE3);
    
    /* 3. Zera os 16 registradores XMM do processador iterativamente */
    for (int i = 0; i < 16; i++) {
        emitir_byte(0x66);
        if (i >= 8) emitir_byte(0x45);
        emitir_byte(0x0F); emitir_byte(0xEF);
        emitir_byte(0xC0 | ((i & 7) << 3) | (i & 7));
    }

    /* 4. Pula para o primeiro slot (offset absoluto) */
    emitir_byte(0xE9);
    uint8_t *next_rip = jit.code + jit.pos + 4;
    uint8_t *dest = jit.code + BOOTSTRAP_SIZE;
    emitir_u32((uint32_t)(dest - next_rip));
}

/* ═══════════════════════════════════════════════════════════════
   EMISSORES AUXILIARES E CONDICIONAIS
   ═══════════════════════════════════════════════════════════════ */
static void emit_alu_rr(uint8_t opcode, uint8_t reg_destino_pqp, uint8_t reg_origem_pqp) {
    uint8_t dst_enc = codificar_reg_x86(reg_destino_pqp);
    uint8_t src_enc = codificar_reg_x86(reg_origem_pqp);
    int dst_hi = precisa_rex(reg_destino_pqp), src_hi = precisa_rex(reg_origem_pqp);

    if (dst_hi || src_hi) {
        emitir_byte(0x40 | (src_hi ? 0x04 : 0) | (dst_hi ? 0x01 : 0));
    }
    emitir_byte(opcode);
    emitir_byte(0xC0 | (src_enc << 3) | dst_enc);
}

static void emit_branch(uint8_t opcode_prefix, uint8_t opcode_suffix, uint32_t target_program_counter, int use_two_byte_opcode) {
    if (use_two_byte_opcode) { emitir_byte(opcode_prefix); emitir_byte(opcode_suffix); } 
    else { emitir_byte(opcode_prefix); }
    
    uint8_t *next_rip = jit.code + jit.pos + 4;
    uint8_t *dest_rip;
    
    if (target_program_counter >= MAX_INST * 4) { 
        dest_rip = jit.code + jit.size + 1024; 
    } else {
        dest_rip = jit.code + BOOTSTRAP_SIZE + (target_program_counter * (SLOT_BYTES / 4)); 
    }
    
    int32_t disp32 = (int32_t)(dest_rip - next_rip);
    emitir_i32_rel(disp32);
}

/* ═══════════════════════════════════════════════════════════════
   ROTINAS DE ENCERRAMENTO E HANDLERS JIT
   ═══════════════════════════════════════════════════════════════ */
static void do_exit(ucontext_t *ctx, uint32_t PC_exit) {
    uint8_t *float_env = (uint8_t *)ctx->uc_mcontext.fpregs;
    uint32_t *vec_state = (uint32_t *)(float_env + 160);

    uint32_t max_count = 0;
    for (uint32_t i = 0; i < xlat_count; i++) {
        uint32_t c = vec_state[xlat_order[i] / 4];
        if (c > max_count) max_count = c;
    }

    int max_digits = snprintf(NULL, 0, "%u", max_count);
    char fmt_inst[64], fmt_exit[64];
    sprintf(fmt_inst, "%%%du:%%04x:%%s", max_digits+5);
    sprintf(fmt_exit, "%%%ds:%%04x:exit\n", max_digits+5);

    for (uint32_t i = 0; i < xlat_count; i++) {
        for (uint32_t j = i + 1; j < xlat_count; j++) {
            if (xlat_order[i] > xlat_order[j]) {
                uint32_t t = xlat_order[i]; xlat_order[i] = xlat_order[j]; xlat_order[j] = t;
            }
        }
    }

    for (uint32_t i = 0; i < xlat_count; i++) {
        uint32_t pc  = xlat_order[i];
        uint32_t cnt = vec_state[pc / 4];
        fprintf(g_output, fmt_inst, cnt, pc, xlat_str[pc]);
    }

    fprintf(g_output, fmt_exit, "-", PC_exit);
    fprintf(g_output, "\n");
    for (int i = 0; i < 15; i++)
        fprintf(g_output, "r%d=%08x,", i, (uint32_t)ctx->uc_mcontext.gregs[PQP_TO_CONTEXT[i]]);
    fprintf(g_output, "r15=%08x\n", (uint32_t)ctx->uc_mcontext.gregs[PQP_TO_CONTEXT[15]]);

    exit(0);
}
static void jit_compile(int sinal_recebido, siginfo_t *info, void *ponteiro_contexto) {
    ucontext_t *ctx = (ucontext_t *)ponteiro_contexto;

    uint64_t rip      = (uint64_t)ctx->uc_mcontext.gregs[REG_RIP];
    uint64_t slot_off = rip - 1 - (uint64_t)jit.code - BOOTSTRAP_SIZE;
    uint32_t slot_idx = (uint32_t)(slot_off / SLOT_BYTES);
    uint32_t PC       = slot_idx * 4;

    if (PC >= PQP_MEM_SIZE) { 
        do_exit(ctx, PC);
        return; 
    }

    xlat_order[xlat_count++] = PC;
    reposicionar_jit(BOOTSTRAP_SIZE + (slot_idx * SLOT_BYTES));
    uint64_t start_pos = jit.pos;

    emit_fast_profiler(PC);

    uint8_t  opcode  = PICO_MEMO[PC];
    uint8_t  byte1   = PICO_MEMO[PC + 1];
    int16_t  imm16   = *(int16_t *)&PICO_MEMO[PC + 2];
    
    uint8_t  rD = byte1 >> 4;
    uint8_t  rS = byte1 & 0x0F;
    uint32_t PC_next = PC + 4;
    
    /* Variáveis Auxiliares Usadas no Switch */
    uint8_t d_enc, s_enc;
    int d_hi, s_hi;
    uint32_t PC_target;
    uint8_t shift;

    /* =========================================================================
        SWITCH DE TRADUÇÃO 
       ========================================================================= */
    switch (opcode) {
        case 0x00: /* mov imm */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "mov_r%u,%08x\n", rD, (uint32_t)(int32_t)imm16);
            d_enc = codificar_reg_x86(rD);
            if (precisa_rex(rD)) emitir_byte(0x41);
            emitir_byte(0xB8 + d_enc);
            emitir_i32_sx(imm16);
            break;

        case 0x01: /* mov r */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "mov_r%u,r%u\n", rD, rS);
            emit_alu_rr(0x89, rD, rS);
            break;

        case 0x02: /* load */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "mov_r%u,[r%u]\n", rD, rS);
            d_enc = codificar_reg_x86(rD); s_enc = codificar_reg_x86(rS);
            d_hi = precisa_rex(rD); s_hi = precisa_rex(rS);
            if (d_hi || s_hi) emitir_byte(0x40 | (d_hi ? 0x04 : 0) | (s_hi ? 0x01 : 0));
            emitir_byte(0x8B);
            if (s_enc == 4) { emitir_byte(0x80 | (d_enc << 3) | 4); emitir_byte(0x24); } 
            else { emitir_byte(0x80 | (d_enc << 3) | s_enc); }
            emitir_u32((uint32_t)(uintptr_t)PICO_MEMO);
            break;

        case 0x03: /* store */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "mov_[r%u],r%u\n", rD, rS);
            s_enc = codificar_reg_x86(rS); d_enc = codificar_reg_x86(rD);
            s_hi = precisa_rex(rS); d_hi = precisa_rex(rD);
            if (s_hi || d_hi) emitir_byte(0x40 | (s_hi ? 0x04 : 0) | (d_hi ? 0x01 : 0));
            emitir_byte(0x89);
            if (d_enc == 4) { emitir_byte(0x80 | (s_enc << 3) | 4); emitir_byte(0x24); } 
            else { emitir_byte(0x80 | (s_enc << 3) | d_enc); }
            emitir_u32((uint32_t)(uintptr_t)PICO_MEMO);
            break;

        case 0x04: /* cmp */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "cmp_r%u<=>r%u\n", rD, rS);
            emit_alu_rr(0x39, rD, rS);
            break;

        case 0x05: /* jmp */
            PC_target = (PC_next + (int32_t)imm16) & 0xFFFF;
            snprintf(xlat_str[PC], sizeof xlat_str[0], "jmp_%04x\n", PC_target);
            emit_branch(0xE9, 0, PC_target, 0);
            break;

        case 0x06: /* jg */
            PC_target = (PC_next + (int32_t)imm16) & 0xFFFF;
            snprintf(xlat_str[PC], sizeof xlat_str[0], "jg_%04x\n", PC_target);
            emit_branch(0x0F, 0x8F, PC_target, 1);
            break;

        case 0x07: /* jl */
            PC_target = (PC_next + (int32_t)imm16) & 0xFFFF;
            snprintf(xlat_str[PC], sizeof xlat_str[0], "jl_%04x\n", PC_target);
            emit_branch(0x0F, 0x8C, PC_target, 1);
            break;

        case 0x08: /* je */
            PC_target = (PC_next + (int32_t)imm16) & 0xFFFF;
            snprintf(xlat_str[PC], sizeof xlat_str[0], "je_%04x\n", PC_target);
            emit_branch(0x0F, 0x84, PC_target, 1);
            break;

        case 0x09: /* add */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "add_r%u+=r%u\n", rD, rS);
            emit_alu_rr(0x01, rD, rS);
            break;

        case 0x0A: /* sub */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "sub_r%u-=r%u\n", rD, rS);
            emit_alu_rr(0x29, rD, rS);
            break;

        case 0x0B: /* and */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "and_r%u&=r%u\n", rD, rS);
            emit_alu_rr(0x21, rD, rS);
            break;

        case 0x0C: /* or */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "or_r%u|=r%u\n", rD, rS);
            emit_alu_rr(0x09, rD, rS);
            break;

        case 0x0D: /* xor */
            snprintf(xlat_str[PC], sizeof xlat_str[0], "xor_r%u^=r%u\n", rD, rS);
            emit_alu_rr(0x31, rD, rS);
            break;

        case 0x0E: /* sal */
            shift = PICO_MEMO[PC + 3] & 0x0F;
            snprintf(xlat_str[PC], sizeof xlat_str[0], "sal_r%u<<=%u\n", rD, shift);
            d_enc = codificar_reg_x86(rD);
            if (precisa_rex(rD)) emitir_byte(0x41);
            emitir_byte(0xC1); emitir_byte((4 << 3) | 0xC0 | d_enc); emitir_byte(shift);
            break;

        case 0x0F: /* sar */
            shift = PICO_MEMO[PC + 3] & 0x0F;
            snprintf(xlat_str[PC], sizeof xlat_str[0], "sar_r%u>>=%u\n", rD, shift);
            d_enc = codificar_reg_x86(rD);
            if (precisa_rex(rD)) emitir_byte(0x41);
            emitir_byte(0xC1); emitir_byte((7 << 3) | 0xC0 | d_enc); emitir_byte(shift);
            break;

        default:
            snprintf(xlat_str[PC], sizeof xlat_str[0], "nop\n");
            break;
    }

    /* Enchimento rápido do Slot com Jumps curtos */
    uint32_t current_len = jit.pos - start_pos;
    if (current_len < SLOT_BYTES) {
        uint32_t remaining = SLOT_BYTES - current_len;
        
        if (remaining >= 2) {
                emitir_byte(0xEB); 
                emitir_byte(remaining - 2); 
            while ((jit.pos - start_pos) < SLOT_BYTES) {
                    emitir_byte(0x90);
            }
        } else {
                while ((jit.pos - start_pos) < SLOT_BYTES) emitir_byte(0x90);
        }
    }

    ctx->uc_mcontext.gregs[REG_RIP] = (greg_t)(jit.code + start_pos);
}


/* ═══════════════════════════════════════════════════════════════
   MAIN E INICIALIZAÇÃO
   ═══════════════════════════════════════════════════════════════ */
static void setup_signals() {
    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);

    struct sigaction config_breakpoint = {0}; 
    struct sigaction config_falha_memoria = {0};

    config_breakpoint.sa_sigaction = jit_compile; 
    config_breakpoint.sa_flags     = SA_ONSTACK | SA_SIGINFO | SA_RESTART;
    sigaction(SIGTRAP, &config_breakpoint, NULL);

    config_falha_memoria.sa_sigaction = jit_compile; 
    config_falha_memoria.sa_flags     = SA_ONSTACK | SA_SIGINFO;
    sigaction(SIGSEGV, &config_falha_memoria, NULL);
}

int main(int argc, char *argv[]) {
    g_input  = fopen(argv[1], "r");
    g_output = fopen(argv[2], "w");

    setup_signals();

    PICO_MEMO = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

    uint32_t n = 0;
    while (fgets(g_line_buf, sizeof g_line_buf, g_input)) {
        unsigned int b[4];
        if (sscanf(g_line_buf, "%x %x %x %x", &b[0], &b[1], &b[2], &b[3]) == 4) {
            PICO_MEMO[n++] = (uint8_t)b[0];
            PICO_MEMO[n++] = (uint8_t)b[1];
            PICO_MEMO[n++] = (uint8_t)b[2];
            PICO_MEMO[n++] = (uint8_t)b[3];
        }
    }

    size_t code_size = BOOTSTRAP_SIZE + (MAX_INST * SLOT_BYTES);
    size_t total = code_size + 64; 

    jit.code = mmap(NULL, total, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    memset(jit.code, 0xCC, code_size); 

    vector_lanes = (uint32_t*)(jit.code + code_size);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            vector_lanes[i * 4 + j] = (i == j) ? 1 : 0;
        }
    }

    build_bootstrap();

    ((void (*)(void))(jit.code))();

    return 0;
}
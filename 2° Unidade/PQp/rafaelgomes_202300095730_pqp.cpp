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
static const uint8_t PQP_TO_UCONTEXT[16] = {
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
static inline void emit_seek(uint64_t off) { jit.pos = off; }
static inline void emit8(uint8_t b)        { jit.code[jit.pos++] = b; }
static inline void emit32(uint32_t v)      { *(uint32_t *)&jit.code[jit.pos] = v; jit.pos += 4; }
static inline void emit_imm16sx(int16_t v) { *(uint32_t *)&jit.code[jit.pos] = (uint32_t)(int32_t)v; jit.pos += 4; }
static inline void emit_rel32(int32_t rel) { *(int32_t *)&jit.code[jit.pos] = rel; jit.pos += 4; }

static inline uint8_t x86_reg_enc(uint8_t pqp_idx) { return PQP_TO_X86[pqp_idx] & 7; }
static inline int needs_rex(uint8_t pqp_idx)       { return PQP_TO_X86[pqp_idx] >= 8; }

/* Injetor de Profiler Massivo via Registradores Vetoriais  */
static inline void emit_fast_profiler(uint32_t PC) {
    uint32_t inst_id = PC / 4;
    uint32_t x_reg   = inst_id / 4;
    uint32_t l_idx   = inst_id % 4;

    if (x_reg >= 8) {
        emit8(0x66); emit8(0x44); emit8(0x0F); emit8(0xFE);
        emit8(0x05 | ((x_reg - 8) << 3));
    } else {
        emit8(0x66); emit8(0x0F); emit8(0xFE);
        emit8(0x05 | (x_reg << 3));
    }

    uint8_t *next_rip = jit.code + jit.pos + 4;
    uint8_t *target_mem = (uint8_t *)&vector_lanes[l_idx * 4];
    emit32((uint32_t)(target_mem - next_rip));
}

/* ═══════════════════════════════════════════════════════════════
   GERAÇÃO DO BOOTSTRAP DINÂMICO
   ═══════════════════════════════════════════════════════════════ */
static void build_bootstrap() {
    emit_seek(0); 
    
    /* 1. Zera GPRs nativos */
    for (int i = 15; i >= 0; i--) {
        uint8_t enc = x86_reg_enc(i);
        if (needs_rex(i)) emit8(0x4D); else emit8(0x48);
        emit8(0x29);
        emit8(0xC0 | (enc << 3) | enc);
    }
    
    /* 2. Reseta o Floating Point Unit nativo */
    emit8(0xDB); emit8(0xE3);
    
    /* 3. Zera os 16 registradores XMM do processador iterativamente */
    for (int i = 0; i < 16; i++) {
        emit8(0x66);
        if (i >= 8) emit8(0x45);
        emit8(0x0F); emit8(0xEF);
        emit8(0xC0 | ((i & 7) << 3) | (i & 7));
    }

    /* 4. Pula para o primeiro slot (offset absoluto) */
    emit8(0xE9);
    uint8_t *next_rip = jit.code + jit.pos + 4;
    uint8_t *dest = jit.code + BOOTSTRAP_SIZE;
    emit32((uint32_t)(dest - next_rip));
}

/* ═══════════════════════════════════════════════════════════════
   DISPATCH TABLE - Emissores Nativos para cada Instrução
   ═══════════════════════════════════════════════════════════════ */
typedef void (*emitter_func_t)(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next);
static emitter_func_t dispatch[16];

static void emit_alu_rr(uint8_t opcode, uint8_t dst_pqp, uint8_t src_pqp) {
    uint8_t dst_enc = x86_reg_enc(dst_pqp);
    uint8_t src_enc = x86_reg_enc(src_pqp);
    int dst_hi = needs_rex(dst_pqp), src_hi = needs_rex(src_pqp);

    if (dst_hi || src_hi) {
        emit8(0x40 | (src_hi ? 0x04 : 0) | (dst_hi ? 0x01 : 0));
    }
    emit8(opcode);
    emit8(0xC0 | (src_enc << 3) | dst_enc);
}

/* O guardrail condicional nativo contra saltos infinitos/fora da área */
static void emit_branch(uint8_t opc1, uint8_t opc2, uint32_t target_PC, int two_byte) {
    if (two_byte) { emit8(opc1); emit8(opc2); } 
    else { emit8(opc1); }
    
    uint8_t *next_rip = jit.code + jit.pos + 4;
    uint8_t *dest_rip;
    
    if (target_PC >= MAX_INST * 4) { 
        /* Caiu na Trap nativa do JIT -> ejetar a Matrix via SIGSEGV */
        dest_rip = jit.code + jit.size + 1024; 
    } else {
        /* Deslocamento Matemático atualizado para o Slot de 32 Bytes! 
         * (SLOT_BYTES / 4) resolve o fator de multiplicação automaticamente 
         */
        dest_rip = jit.code + BOOTSTRAP_SIZE + (target_PC * (SLOT_BYTES / 4)); 
    }
    
    int32_t disp32 = (int32_t)(dest_rip - next_rip);
    emit_rel32(disp32);
}

static void op_00_mov_imm(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "mov_r%u,%08x\n", rD, (uint32_t)(int32_t)imm);
    uint8_t enc = x86_reg_enc(rD);
    if (needs_rex(rD)) emit8(0x41);
    emit8(0xB8 + enc);
    emit_imm16sx(imm);
}

static void op_01_mov_r(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "mov_r%u,r%u\n", rD, rS);
    emit_alu_rr(0x89, rD, rS);
}

static void op_02_load(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "mov_r%u,[r%u]\n", rD, rS);
    uint8_t d_enc = x86_reg_enc(rD), s_enc = x86_reg_enc(rS);
    int d_hi = needs_rex(rD), s_hi = needs_rex(rS);
    if (d_hi || s_hi) emit8(0x40 | (d_hi ? 0x04 : 0) | (s_hi ? 0x01 : 0));
    
    emit8(0x8B);
    if (s_enc == 4) { emit8(0x80 | (d_enc << 3) | 4); emit8(0x24); } 
    else { emit8(0x80 | (d_enc << 3) | s_enc); }
    emit32((uint32_t)(uintptr_t)PICO_MEMO);
}

static void op_03_store(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "mov_[r%u],r%u\n", rD, rS);
    uint8_t s_enc = x86_reg_enc(rS), d_enc = x86_reg_enc(rD);
    int s_hi = needs_rex(rS), d_hi = needs_rex(rD);
    if (s_hi || d_hi) emit8(0x40 | (s_hi ? 0x04 : 0) | (d_hi ? 0x01 : 0));
    
    emit8(0x89);
    if (d_enc == 4) { emit8(0x80 | (s_enc << 3) | 4); emit8(0x24); } 
    else { emit8(0x80 | (s_enc << 3) | d_enc); }
    emit32((uint32_t)(uintptr_t)PICO_MEMO);
}

static void op_04_cmp(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "cmp_r%u<=>r%u\n", rD, rS);
    emit_alu_rr(0x39, rD, rS);
}

static void op_05_jmp(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    uint32_t PC_target = (PC_next + (int32_t)imm) & 0xFFFF;
    snprintf(xlat_str[PC], sizeof xlat_str[0], "jmp_%04x\n", PC_target);
    emit_branch(0xE9, 0, PC_target, 0);
}

static void op_06_jg(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    uint32_t PC_target = (PC_next + (int32_t)imm) & 0xFFFF;
    snprintf(xlat_str[PC], sizeof xlat_str[0], "jg_%04x\n", PC_target);
    emit_branch(0x0F, 0x8F, PC_target, 1);
}

static void op_07_jl(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    uint32_t PC_target = (PC_next + (int32_t)imm) & 0xFFFF;
    snprintf(xlat_str[PC], sizeof xlat_str[0], "jl_%04x\n", PC_target);
    emit_branch(0x0F, 0x8C, PC_target, 1);
}

static void op_08_je(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    uint32_t PC_target = (PC_next + (int32_t)imm) & 0xFFFF;
    snprintf(xlat_str[PC], sizeof xlat_str[0], "je_%04x\n", PC_target);
    emit_branch(0x0F, 0x84, PC_target, 1);
}

static void op_09_add(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "add_r%u+=r%u\n", rD, rS);
    emit_alu_rr(0x01, rD, rS);
}

static void op_0a_sub(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "sub_r%u-=r%u\n", rD, rS);
    emit_alu_rr(0x29, rD, rS);
}

static void op_0b_and(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "and_r%u&=r%u\n", rD, rS);
    emit_alu_rr(0x21, rD, rS);
}

static void op_0c_or(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "or_r%u|=r%u\n", rD, rS);
    emit_alu_rr(0x09, rD, rS);
}

static void op_0d_xor(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    snprintf(xlat_str[PC], sizeof xlat_str[0], "xor_r%u^=r%u\n", rD, rS);
    emit_alu_rr(0x31, rD, rS);
}

static void op_0e_sal(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    uint8_t shift = PICO_MEMO[PC + 3] & 0x0F;
    snprintf(xlat_str[PC], sizeof xlat_str[0], "sal_r%u<<=%u\n", rD, shift);
    uint8_t d_enc = x86_reg_enc(rD);
    if (needs_rex(rD)) emit8(0x41);
    emit8(0xC1); emit8((4 << 3) | 0xC0 | d_enc); emit8(shift);
}

static void op_0f_sar(uint32_t PC, uint8_t rD, uint8_t rS, int16_t imm, uint32_t PC_next) {
    uint8_t shift = PICO_MEMO[PC + 3] & 0x0F;
    snprintf(xlat_str[PC], sizeof xlat_str[0], "sar_r%u>>=%u\n", rD, shift);
    uint8_t d_enc = x86_reg_enc(rD);
    if (needs_rex(rD)) emit8(0x41);
    emit8(0xC1); emit8((7 << 3) | 0xC0 | d_enc); emit8(shift);
}

static void init_dispatch_table() {
    dispatch[0x00] = op_00_mov_imm; dispatch[0x01] = op_01_mov_r;
    dispatch[0x02] = op_02_load;    dispatch[0x03] = op_03_store;
    dispatch[0x04] = op_04_cmp;     dispatch[0x05] = op_05_jmp;
    dispatch[0x06] = op_06_jg;      dispatch[0x07] = op_07_jl;
    dispatch[0x08] = op_08_je;      dispatch[0x09] = op_09_add;
    dispatch[0x0A] = op_0a_sub;     dispatch[0x0B] = op_0b_and;
    dispatch[0x0C] = op_0c_or;      dispatch[0x0D] = op_0d_xor;
    dispatch[0x0E] = op_0e_sal;     dispatch[0x0F] = op_0f_sar;
}

/* ═══════════════════════════════════════════════════════════════
   ROTINAS DE ENCERRAMENTO E HANDLERS JIT
   ═══════════════════════════════════════════════════════════════ */
static void do_exit(ucontext_t *ctx, uint32_t PC_exit) {
    /* Leitura Ultra-Rápida direto do Contexto da CPU (fpregs = offset 160) */
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
        fprintf(g_output, "r%d=%08x,", i, (uint32_t)ctx->uc_mcontext.gregs[PQP_TO_UCONTEXT[i]]);
    fprintf(g_output, "r15=%08x\n", (uint32_t)ctx->uc_mcontext.gregs[PQP_TO_UCONTEXT[15]]);

    exit(0);
}

static void jit_compile(int sig, siginfo_t *si, void *ctx_ptr) {
    ucontext_t *ctx = (ucontext_t *)ctx_ptr;

    uint64_t rip      = (uint64_t)ctx->uc_mcontext.gregs[REG_RIP];
    
    /* A instrução INT3 incrementou o RIP em 1, por isso subtraímos 1 */
    uint64_t slot_off = rip - 1 - (uint64_t)jit.code - BOOTSTRAP_SIZE;
    uint32_t slot_idx = (uint32_t)(slot_off / SLOT_BYTES);
    uint32_t PC       = slot_idx * 4;

    /* Se um Jump cair aqui, invocamos o Exit com estado intacto! */
    if (PC >= PQP_MEM_SIZE) { 
        do_exit(ctx, PC);
        return; 
    }

    xlat_order[xlat_count++] = PC;
    emit_seek(BOOTSTRAP_SIZE + (slot_idx * SLOT_BYTES));
    uint64_t start_pos = jit.pos;

    emit_fast_profiler(PC);

    uint8_t  opcode  = PICO_MEMO[PC];
    uint8_t  byte1   = PICO_MEMO[PC + 1];
    /* Captura nativa como signed int16_t (trata saltos negativos sozinhos) */
    int16_t  imm16   = *(int16_t *)&PICO_MEMO[PC + 2];

    dispatch[opcode](PC, byte1 >> 4, byte1 & 0x0F, imm16, PC + 4);

/* Calcula quantos bytes faltam para completar os 32 bytes do slot.
     */
    uint32_t current_len = jit.pos -    start_pos;
    if (current_len < SLOT_BYTES) {
        uint32_t remaining = SLOT_BYTES - current_len;
        
        if (remaining >= 2) {
            /* Injeta um JMP curto (0xEB) que pula o lixo todo numa fração de ciclo */
            emit8(0xEB); 
            emit8(remaining - 2); /* offset do pulo */

            while ((jit.pos - start_pos) < SLOT_BYTES) {
                emit8(0x90);
            }
        } else {
            /* Se por acaso faltar apenas 1 byte, botamos só um NOP */
            while ((jit.pos - start_pos) < SLOT_BYTES) emit8(0x90);
        }
    }

    ctx->uc_mcontext.gregs[REG_RIP] = (greg_t)(jit.code + start_pos);
}

/* Guardrail para falhas nativas do SO caso ocorram */
static void jit_exit(int sig, siginfo_t *si, void *ctx_ptr) {
    ucontext_t *ctx = (ucontext_t *)ctx_ptr;
    uint64_t rip = (uint64_t)ctx->uc_mcontext.gregs[REG_RIP];

    if (rip < (uint64_t)jit.code + BOOTSTRAP_SIZE) return;

    uint32_t PC_exit = (uint32_t)((rip - (uint64_t)jit.code - BOOTSTRAP_SIZE) / SLOT_BYTES) * 4;
    do_exit(ctx, PC_exit);
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

    struct sigaction sa_trap = {0}, sa_segv = {0};
    sa_trap.sa_sigaction = jit_compile;
    sa_trap.sa_flags     = SA_ONSTACK | SA_SIGINFO | SA_RESTART;
    sigaction(SIGTRAP, &sa_trap, NULL);

    sa_segv.sa_sigaction = jit_exit;
    sa_segv.sa_flags     = SA_ONSTACK | SA_SIGINFO;
    sigaction(SIGSEGV, &sa_segv, NULL);
}

int main(int argc, char *argv[]) {
    g_input  = fopen(argv[1], "r");
    g_output = fopen(argv[2], "w");

    init_dispatch_table();
    setup_signals();

    PICO_MEMO = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

    /* Leitura Limpa Padrão - Imune a lixo */
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

    /* * Aloca cobertura integral para o PC de 0x0000 até 0xFFFF. 
     * Toda a área inicializa como INT3 (0xCC), fazendo a proteção
     * total de limites sem dar brecha para o processador fugir.
     */
    size_t code_size = BOOTSTRAP_SIZE + (MAX_INST * SLOT_BYTES);
    size_t total = code_size + 64; 

    jit.code = mmap(NULL, total, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    memset(jit.code, 0xCC, code_size); 

    /* Incorporar estrutura matemática das faixas dos Registradores XMM */
    vector_lanes = (uint32_t*)(jit.code + code_size);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            vector_lanes[i * 4 + j] = (i == j) ? 1 : 0;
        }
    }

    build_bootstrap();

    /* Arranca a VM direto para a rotina nativa em kernel-space */
    ((void (*)(void))(jit.code))();

    return 0;
}
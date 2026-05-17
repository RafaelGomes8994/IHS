#include <stdio.h>
#include <stdint.h>

// Estrutura que representa a arquitetura do PQP
typedef struct {
    uint32_t regs[16];   // 16 registradores de 32 bits
    uint8_t memory[256]; // Memória Von Neumann (256 bytes)
    uint32_t pc;         // Program Counter
    int flag_cmp;        // Flag para saltos (-1 para <, 0 para ==, 1 para >)
    int is_running;      // Status do processador
} PQP_CPU;

void init_cpu(PQP_CPU *cpu) {
    for (int i = 0; i < 16; i++) cpu->regs[i] = 0;
    for (int i = 0; i < 256; i++) cpu->memory[i] = 0;
    cpu->pc = 0;
    cpu->flag_cmp = 0;
    cpu->is_running = 1;
}

// Lida com a leitura da memória little-endian, buscando 4 bytes
uint32_t read_mem_32(PQP_CPU *cpu, uint32_t addr) {
    if (addr > 252) { // 252 é o máximo onde ainda podemos ler 4 bytes (252, 253, 254, 255)
        printf("Erro Fatal: Leitura fora dos limites (Endereco: 0x%04X)\n", addr);
        cpu->is_running = 0;
        return 0;
    }
    return cpu->memory[addr] | 
           (cpu->memory[addr + 1] << 8) | 
           (cpu->memory[addr + 2] << 16) | 
           (cpu->memory[addr + 3] << 24);
}

// Lida com a escrita na memória no formato little-endian, quebrando os 32 bits em 4 bytes
void write_mem_32(PQP_CPU *cpu, uint32_t addr, uint32_t val) {
    if (addr > 252) {
        printf("Erro Fatal: Escrita fora dos limites (Endereco: 0x%04X)\n", addr);
        cpu->is_running = 0;
        return;
    }
    cpu->memory[addr]     = val & 0xFF;
    cpu->memory[addr + 1] = (val >> 8) & 0xFF;
    cpu->memory[addr + 2] = (val >> 16) & 0xFF;
    cpu->memory[addr + 3] = (val >> 24) & 0xFF;
}

void run_interpreter(PQP_CPU *cpu) {
    while (cpu->pc <= 252 && cpu->is_running) {
        
        // FETCH
        uint32_t inst = read_mem_32(cpu, cpu->pc);
        if (!cpu->is_running) break; // Se o FETCH falhar por memory leak, aborta

        // DECODE
        uint8_t opcode = inst & 0xFF;
        uint8_t ry     = (inst >> 8) & 0x0F;
        uint8_t rx     = (inst >> 12) & 0x0F;
        int16_t imm16  = (int16_t)((inst >> 16) & 0xFFFF);
        
        // O PC avança para a próxima instrução
        cpu->pc += 4;

        // EXECUTE
        switch (opcode) {
            case 0x00: // mov rx, i16 (com extensão de sinal)
                cpu->regs[rx] = (int32_t)imm16;
                break;
                
            case 0x01: // mov rx, ry
                cpu->regs[rx] = cpu->regs[ry];
                break;
                
            case 0x02: // mov rx, [ry]
                cpu->regs[rx] = read_mem_32(cpu, cpu->regs[ry]);
                break;
                
            case 0x03: // mov [rx], ry
                write_mem_32(cpu, cpu->regs[rx], cpu->regs[ry]);
                break;
                
            case 0x04: // cmp rx, ry
                if (cpu->regs[rx] < cpu->regs[ry]) cpu->flag_cmp = -1;
                else if (cpu->regs[rx] > cpu->regs[ry]) cpu->flag_cmp = 1;
                else cpu->flag_cmp = 0;
                break;
                
            case 0x05: // jmp i16
                cpu->pc += imm16;
                break;
                
            case 0x06: // jg i16
                if (cpu->flag_cmp == 1) cpu->pc += imm16;
                break;
                
            case 0x07: // jl i16
                if (cpu->flag_cmp == -1) cpu->pc += imm16;
                break;
                
            case 0x08: // je i16
                if (cpu->flag_cmp == 0) cpu->pc += imm16;
                break;
                
            case 0x09: // add rx, ry
                cpu->regs[rx] += cpu->regs[ry];
                break;
                
            case 0x0A: // sub rx, ry
                cpu->regs[rx] -= cpu->regs[ry];
                break;
                
            case 0x0B: // and rx, ry
                cpu->regs[rx] &= cpu->regs[ry];
                break;
                
            case 0x0C: // or rx, ry
                cpu->regs[rx] |= cpu->regs[ry];
                break;
                
            case 0x0D: // xor rx, ry
                cpu->regs[rx] ^= cpu->regs[ry];
                break;
                
            case 0x0E: // sal rx, i5
                // Deslocamento Aritmético a Esquerda (mascarado para max 31 posicoes, ou i5 que é 0x1F)
                if (imm16 > 31) imm16 >>= 8; 
                cpu->regs[rx] <<= imm16;
                break;
                
            case 0x0F: // sar rx, i5
                if (imm16 > 31) imm16 >>= 8;
                cpu->regs[rx] = (int32_t)cpu->regs[rx] >> imm16;
                break; 
                
            default: // Detectou algum opcode falso ou trecho de dados como [FA F0 F0 F0]
                printf("Execucao abortada! Opcode invalido ou memoria de dados: 0x%02X no PC: 0x%04X\n", opcode, cpu->pc - 4);
                cpu->is_running = 0;
                break;
        }
    }
    printf("\n>>> Processador Halt. PC Final: 0x%04X\n", cpu->pc);
}

int main() {
    PQP_CPU cpu;
    init_cpu(&cpu);

    // 1. Carrega o binário na memória do processador
    FILE *file = fopen("pqp.input.txt", "r");
    if (!file) {
        printf("Erro: Nao foi possivel abrir pqp.input.txt\n");
        return 1;
    }

    unsigned int byte_val;
    int index = 0;
    while (fscanf(file, "%i", &byte_val) == 1 && index < 256) {
        cpu.memory[index] = (uint8_t)byte_val;
        index++;
    }
    fclose(file);

    printf("Carregados %d bytes na memoria.\n", index);
    printf("Iniciando interpretacao...\n\n");

    // 2. Roda o interpretador
    run_interpreter(&cpu);

    // 3. Despeja o estado final do processador (Dump dos Registradores)
    printf("\n--- ESTADO FINAL DA CPU ---\n");
    for (int i = 0; i < 16; i++) {
        // Imprime em decimal e hexadecimal para facilitar a depuração
        printf("r%d\t= %10d \t(0x%08X)\n", i, cpu.regs[i], cpu.regs[i]);
    }
    printf("PC\t= %10u \t(0x%04X)\n", cpu.pc, cpu.pc);
    printf("FLAG\t= %10d\n", cpu.flag_cmp);

    return 0;
}
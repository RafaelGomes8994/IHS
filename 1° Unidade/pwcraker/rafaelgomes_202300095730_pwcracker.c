#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

// Configurações do Cracker
#define NUM_THREADS 4
#define MIN_PASS_LEN 2
#define MAX_PASS_LEN 4
#define MAX_USERS 100 
#define hsrand srand
#define hrand rand

// Dicionário Completo: Letras minúsculas, maiúsculas e números
const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const int charset_len = 62;

// Estrutura para guardar os utilizadores
typedef struct {
    char login[64];
    uint8_t hash[8];
    char cracked_password[16];
    bool is_cracked;
} TargetUser;

// --- VARIÁVEIS GLOBAIS DE CONTROLE ---
TargetUser users[MAX_USERS];
int num_users = 0;
volatile int total_cracked = 0; 

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; 

// --- FUNÇÕES PRNG THREAD-SAFE (Versão Linux rand_r) ---
_Thread_local unsigned int thread_seed = 0;

void hsrand(uint32_t seed) {
    thread_seed = seed;
}

uint32_t hrand() {
    return rand_r(&thread_seed); 
}

// --- FUNÇÃO DE HASH MAO-64 ---
void MAO_64(uint8_t* hash, const char* senha) {
    uint32_t i, n = strlen(senha), nr = 32, seed = 0;
    memset(hash, 0, 8); 
    for(i = 0; i < n; i++)
        seed = (seed << 8) | ((seed >> 24) ^ senha[i]);
    hsrand(seed);
    for(i = 0; i < nr; i++)
        hash[i & 0b111] = hash[i & 0b111] ^ hrand();
}

// --- ESTRUTURA PARA AS THREADS ---
typedef struct {
    int thread_id;
    int start_idx; 
    int end_idx;   
} ThreadArgs;

// --- FUNÇÃO RECURSIVA DE FORÇA BRUTA ---
void brute_force(char* current_pass, int pos, int max_len) {
    if (total_cracked >= num_users) return;

    if (pos >= MIN_PASS_LEN) {
        uint8_t current_hash[8];
        MAO_64(current_hash, current_pass);

        // Verifica o hash gerado contra os hashes de todos os utilizadores não quebrados
        for (int i = 0; i < num_users; i++) {
            if (!users[i].is_cracked) {
                bool match = true;
                for (int j = 0; j < 8; j++) {
                    if (current_hash[j] != users[i].hash[j]) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    pthread_mutex_lock(&lock);
                    if (!users[i].is_cracked) { 
                        users[i].is_cracked = true;
                        strcpy(users[i].cracked_password, current_pass);
                        total_cracked++;
                        // AVISO DO PRINTF CORRIGIDO AQUI: %lu em vez de %d
                        printf("[Thread %lu] Senha quebrada! %s:%s\n", (unsigned long)pthread_self(), users[i].login, current_pass);
                    }
                    pthread_mutex_unlock(&lock);
                }
            }
        }
    }

    if (pos == max_len) return; 

    for (int i = 0; i < charset_len; i++) {
        current_pass[pos] = charset[i];
        current_pass[pos + 1] = '\0';
        brute_force(current_pass, pos + 1, max_len);
    }
}

// --- ROTINA PRINCIPAL DA THREAD ---
void* thread_worker(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    char pass_buffer[MAX_PASS_LEN + 1] = {0};

    // Repartição do trabalho pelo dicionário
    for (int i = args->start_idx; i < args->end_idx; i++) {
        if (total_cracked >= num_users) break;
        
        pass_buffer[0] = charset[i];
        pass_buffer[1] = '\0';
        brute_force(pass_buffer, 1, MAX_PASS_LEN);
    }

    pthread_exit(NULL);
}

// --- FUNÇÃO MAIN ---
int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Uso: %s <entrada.txt> <saida.txt>\n", argv[0]);
        return 1;
    }

    // 1. LER FICHEIRO DE ENTRADA
    FILE *input_file = fopen(argv[1], "r");
    if (!input_file) {
        printf("Erro ao abrir %s\n", argv[1]);
        return 1;
    }

    int expected_users = 0;
    
    // Ler a quantidade de contas 'n' (1ª linha)
    if (fscanf(input_file, "%d", &expected_users) != 1) {
        printf("Erro: Ficheiro nao contem a quantidade inicial de contas.\n");
        fclose(input_file);
        return 1;
    }

    // Ler as 'n' contas linha a linha
    for (int i = 0; i < expected_users && num_users < MAX_USERS; i++) {
        char hex_hash[17] = {0};
        char login_buf[64] = {0};

        // O espaço inicial em " %[^:]" é crucial para ignorar as quebras de linha anteriores (\n)
        if (fscanf(input_file, " %[^:]:%16s", login_buf, hex_hash) == 2) {
            strcpy(users[num_users].login, login_buf);
            
            // Converte string Hexadecimal para array de bytes
            for (int j = 0; j < 8; j++) {
                sscanf(&hex_hash[j * 2], "%2hhx", &users[num_users].hash[j]);
            }
            users[num_users].is_cracked = false;
            num_users++;
        }
    }
    fclose(input_file);

    if (num_users == 0) {
        printf("Nenhum utilizador valido lido do ficheiro.\n");
        return 1;
    }

    printf("Iniciando ataque para %d utilizadores...\n", num_users);

    // 2. DISPARAR THREADS
    pthread_t threads[NUM_THREADS];
    ThreadArgs thread_args[NUM_THREADS];
    int chunk_size = charset_len / NUM_THREADS;

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].start_idx = i * chunk_size;
        thread_args[i].end_idx = (i == NUM_THREADS - 1) ? charset_len : (i + 1) * chunk_size;

        pthread_create(&threads[i], NULL, thread_worker, (void*)&thread_args[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // 3. GRAVAR O RESULTADO EXATO NO FICHEIRO DE SAÍDA
    FILE *output_file = fopen(argv[2], "w");
    if (!output_file) {
        printf("Erro ao criar %s\n", argv[2]);
        return 1;
    }

    for (int i = 0; i < num_users; i++) {
        if (users[i].is_cracked) {
            fprintf(output_file, "%s:%s\n", users[i].login, users[i].cracked_password);
        } else {
            fprintf(output_file, "%s:NAO_ENCONTRADA\n", users[i].login);
        }
    }
    fclose(output_file);

    printf("Operacao concluida com sucesso. Resultados guardados em '%s'.\n", argv[2]);
    return 0;
}
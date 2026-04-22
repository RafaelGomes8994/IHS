#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

// --- VARIÁVEIS GLOBAIS DE CONFIGURAÇÃO ---
int numeroThreads = 4;

int tamanhoMaxSenha = 4;

const char alfabeto[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const int tamanhoAlfabeto = 62;

typedef struct {
    char login[64];
    // hashAlvo: to usando uint64_t aqui pra comparação é feita em UMA operação de CPU, não em strcmp().
    uint64_t hashAlvo;
    char senhaDescoberta[16];
    bool foiDescoberta;
    int ordemOriginal;
} UsuarioAlvo;

// usuariosAlvo: to usando como global pra todas as threads conseguirem acessar a lista de usuarios sem passar parametro.
UsuarioAlvo* usuariosAlvo = NULL;
int quantidadeUsuarios = 0;

// totalDescobertos: to usando volatile pra compilador não cachear esse valor, porque as threads tão sempre lendo/escrevendo nele.
volatile int totalDescobertos = 0;

// lock: to usando mutex aqui pra quando uma thread acha uma senha, outra não sobrescreve ao mesmo tempo. Evita bagunça.
pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;

// --- FUNÇÃO PARA ORDENAR OS UTILIZADORES PELO HASH ---
int compararHashes(const void *a, const void *b) {
    uint64_t hashA = ((UsuarioAlvo*)a)->hashAlvo;
    uint64_t hashB = ((UsuarioAlvo*)b)->hashAlvo;
    if (hashA < hashB) return -1;
    if (hashA > hashB) return 1;
    return 0;
}

int compararOrdemOriginal(const void *a, const void *b) {
    int ordemA = ((UsuarioAlvo*)a)->ordemOriginal;
    int ordemB = ((UsuarioAlvo*)b)->ordemOriginal;
    if (ordemA < ordemB) return -1;
    if (ordemA > ordemB) return 1;
    return 0;
}

// --- FUNÇÃO DE HASH MAO-64 ---
// Implementação pedida: usa semente baseada na senha e 32 rodadas sobre 8 bytes de hash.
static _Thread_local uint32_t estadoPRNG = 0;

static inline void hsrand(uint32_t seed) {
    estadoPRNG = seed;
}

static inline uint8_t hrand(void) {
    estadoPRNG = 1103515245u * estadoPRNG + 12345u;
    return (uint8_t)((estadoPRNG & 0xFFu) ^
                     ((estadoPRNG >> 8) & 0xFFu) ^
                     ((estadoPRNG >> 16) & 0xFFu) ^
                     ((estadoPRNG >> 24) & 0xFFu));
}

void MAO_64(uint8_t* hash, const char* senha) {
    uint32_t i, n = (uint32_t)strlen(senha), nr = 32, seed = 0;

    memset(hash, 0, 8);

    for (i = 0; i < n; i++) {
        seed = (seed << 8) | ((seed >> 24) ^ senha[i]);
    }

    hsrand(seed);

    for (i = 0; i < nr; i++) {
        hash[i & 0b111] = (uint8_t)(hash[i & 0b111] ^ hrand());
    }
}

static inline uint64_t gerarHashMao64(const char* senha) {
    uint8_t hash[8];
    MAO_64(hash, senha);
    return ((uint64_t)hash[0] << 56) | ((uint64_t)hash[1] << 48) | ((uint64_t)hash[2] << 40) |
           ((uint64_t)hash[3] << 32) | ((uint64_t)hash[4] << 24) | ((uint64_t)hash[5] << 16) |
           ((uint64_t)hash[6] << 8)  | ((uint64_t)hash[7]);
}

// to usando isso pra cada thread saber qual "fatia" do charset ela tem que processar (Thread 0 pega a-f, Thread 1 pega g-l, etc).
typedef struct {
    int inicioIndice;
    int fimIndice;
} ArgumentosThread;

// to usando isso pra gerar todas as combinações de senhas recursivamente e testar cada uma em busca binária. (magia negra mesmo)
void quebrandoesseMAOpelaraiz(char* senhaAtual, int posicao, int tamanhoMaximo) {
    // to checando aqui pra se todo mundo já foi crackeado, saio cedo e não desperdição CPU.
    if (totalDescobertos >= quantidadeUsuarios) return;

    // to transformando a senha em hash 64-bit pra comparar com UMA operação (não strcmp que é lento).
    uint64_t hashAtual64 = gerarHashMao64(senhaAtual);

    // to usando busca binária aqui: sem ela seria O(n), com ela é O(log n). 1000 usuarios? só 10 comparações!
    int esquerda = 0;
    int direita = quantidadeUsuarios - 1;
    
    while (esquerda <= direita) {
        int meio = esquerda + (direita - esquerda) / 2;
        
        if (usuariosAlvo[meio].hashAlvo == hashAtual64) {
            // achei um match! to bloqueando com mutex pra outra thread não pisar aqui do mesmo jeito.
            pthread_mutex_lock(&trava);
            if (!usuariosAlvo[meio].foiDescoberta) {
                usuariosAlvo[meio].foiDescoberta = true;
                strcpy(usuariosAlvo[meio].senhaDescoberta, senhaAtual);
                totalDescobertos++;
            }
            
            // to procurando duplicatas porque múltiplos usuarios podem ter a mesma senha/hash. Preciso crackear TODOS.
            // Procurando à esquerda do match
            int temporario = meio - 1;
            while(temporario >= 0 && usuariosAlvo[temporario].hashAlvo == hashAtual64) {
                if (!usuariosAlvo[temporario].foiDescoberta) {
                    usuariosAlvo[temporario].foiDescoberta = true;
                    strcpy(usuariosAlvo[temporario].senhaDescoberta, senhaAtual);
                    totalDescobertos++;
                }
                temporario--;
            }
            
            // Procurando à direita do match
            temporario = meio + 1;
            while(temporario < quantidadeUsuarios && usuariosAlvo[temporario].hashAlvo == hashAtual64) {
                if (!usuariosAlvo[temporario].foiDescoberta) {
                    usuariosAlvo[temporario].foiDescoberta = true;
                    strcpy(usuariosAlvo[temporario].senhaDescoberta, senhaAtual);
                    totalDescobertos++;
                }
                temporario++;
            }
            pthread_mutex_unlock(&trava);
            break; // achei, saio da busca binária
        }
        
        // to dividindo em duas partes aqui, busca binária classica mesmo.
        if (usuariosAlvo[meio].hashAlvo < hashAtual64) {
            esquerda = meio + 1;
        } else {
            direita = meio - 1;
        }
    }

    // parei se chegou no tamanho máximo.
    if (posicao == tamanhoMaximo) return;

    // to gerando recursivamente todas as combinações: a, b, c..., aa, ab, ac..., aaa, etc.
    for (int i = 0; i < tamanhoAlfabeto; i++) {
        senhaAtual[posicao] = alfabeto[i];
        senhaAtual[posicao + 1] = '\0';
        quebrandoesseMAOpelaraiz(senhaAtual, posicao + 1, tamanhoMaximo);
    }
}

// --- ROTINA PRINCIPAL DA THREAD ---
// to usando isso pra cada thread processar sua porção de senhas em paralelo. Thread 1 testa a-f, Thread 2 testa g-l, etc.
void* trabalhadorThread(void* argumento) {
    ArgumentosThread* dados = (ArgumentosThread*)argumento;
    
    // to alocando buffer local aqui pra cada thread ter seu próprio, não pisam uma na outra.
    char* bufferSenha = (char*)calloc(tamanhoMaxSenha + 2, sizeof(char));

    // to processando minha fatia do charset...
    for (int i = dados->inicioIndice; i < dados->fimIndice; i++) {
        // saio cedo se já achei todo mundo.
        if (totalDescobertos >= quantidadeUsuarios) break;
        
        // comeco uma árvore de recursão com cada letra do meu intervalo.
        bufferSenha[0] = alfabeto[i];
        bufferSenha[1] = '\0';
        quebrandoesseMAOpelaraiz(bufferSenha, 1, tamanhoMaxSenha);
    }

    free(bufferSenha);
    pthread_exit(NULL);
}

// --- FUNÇÃO MAIN ---
// to usando isso pra ler dados, ordenar, rodar threads e salvar resultados. É o "maestro" da operação.
int main(int argc, char* argv[]) {
    // se argumentos tiverem errado, saio silenciosamente. Esperado: entrada, saída, threads (opt), tamanho (opt)
    if (argc < 3 || argc > 5) return 1;

    // to lendo argumentos, com defaults de 4 threads e tamanho 4.
    if (argc >= 4) numeroThreads = atoi(argv[3]);
    if (argc == 5) tamanhoMaxSenha = atoi(argv[4]);

    // validando pra não deixar valores loucos passarem.
    if (numeroThreads < 1) numeroThreads = 1;
    if (numeroThreads > tamanhoAlfabeto) numeroThreads = tamanhoAlfabeto; // não faz sentido ter mais threads que chars
    if (tamanhoMaxSenha < 1) tamanhoMaxSenha = 1;

    // abrindo arquivo de entrada com formato login:hash
    FILE *arquivoEntrada = fopen(argv[1], "r");
    if (!arquivoEntrada) return 1;

    // primeira linha é a quantidade de usuarios.
    int usuariosEsperados = 0;
    if (fscanf(arquivoEntrada, "%d", &usuariosEsperados) != 1) {
        fclose(arquivoEntrada);
        return 1;
    }

    // alocando dinamicamente porque não sei quantos usuarios tem até ler.
    usuariosAlvo = (UsuarioAlvo*) malloc(usuariosEsperados * sizeof(UsuarioAlvo));
    if (!usuariosAlvo) {
        fclose(arquivoEntrada);
        return 1;
    }

    // lendo cada linha no format login:hash_hex
    for (int i = 0; i < usuariosEsperados; i++) {
        char hashHex[17] = {0};
        char loginBuffer[64] = {0};

        // to usando %63[^:] pra ler até 63 chars até ':', evita buffer overflow.
        if (fscanf(arquivoEntrada, " %63[^:]:%16s", loginBuffer, hashHex) == 2) {
            strcpy(usuariosAlvo[quantidadeUsuarios].login, loginBuffer);
            // convertendo hex string (ex: "1A2B") pra uint64_t.
            usuariosAlvo[quantidadeUsuarios].hashAlvo = strtoull(hashHex, NULL, 16);
            usuariosAlvo[quantidadeUsuarios].foiDescoberta = false;
            usuariosAlvo[quantidadeUsuarios].ordemOriginal = quantidadeUsuarios;
            quantidadeUsuarios++;
        }
    }
    fclose(arquivoEntrada);

    // se ninguém foi lido, saio.
    if (quantidadeUsuarios == 0) {
        free(usuariosAlvo);
        return 1;
    }

    // PASSO CRÍTICO: to ordenando os hashes pra busca binária funcionar. Sem isso viro O(n) em vez de O(log n)!
    qsort(usuariosAlvo, quantidadeUsuarios, sizeof(UsuarioAlvo), compararHashes);

    // CRIAR E DISPARAR AS THREADS
    // to alocando arrays pra guardar as threads e seus parametros.
    pthread_t* vetorThreads = (pthread_t*) malloc(numeroThreads * sizeof(pthread_t));
    ArgumentosThread* argumentosThreads = (ArgumentosThread*) malloc(numeroThreads * sizeof(ArgumentosThread));
    
    // calculando quantos chars cada thread vai processar (62 chars / 4 threads = ~15 each).
    int tamanhoFatia = tamanhoAlfabeto / numeroThreads;

    // criando cada thread com sua fatia do charset.
    for (int i = 0; i < numeroThreads; i++) {
        // dividindo os indices: Thread 0 [0-15], Thread 1 [15-30], etc.
        argumentosThreads[i].inicioIndice = i * tamanhoFatia;
        argumentosThreads[i].fimIndice = (i == numeroThreads - 1) ? tamanhoAlfabeto : (i + 1) * tamanhoFatia;
        
        // criando a thread que vai rodar trabalhadorThread() com sua fatia.
        pthread_create(&vetorThreads[i], NULL, trabalhadorThread, (void*)&argumentosThreads[i]);
    }

    // AGUARDANDO TODAS AS THREADS
    // to bloqueando aqui até todas terminarem. Precisa disso antes de salvar resultados.
    for (int i = 0; i < numeroThreads; i++) {
        pthread_join(vetorThreads[i], NULL);
    }

    // retornando ao ordenamento original de leitura para gravar no mesmo formato do input.
    qsort(usuariosAlvo, quantidadeUsuarios, sizeof(UsuarioAlvo), compararOrdemOriginal);

    // GRAVAR OS RESULTADOS
    // abrindo arquivo pra escrever resultado de cada usuario (login:senha ou login:NOT_FOUND).
    FILE *arquivoSaida = fopen(argv[2], "w");
    if (!arquivoSaida) {
        free(vetorThreads); free(argumentosThreads); free(usuariosAlvo);
        return 1;
    }

    // gravando resultado: cracked ou NOT_FOUND.
    for (int i = 0; i < quantidadeUsuarios; i++) {
        if (usuariosAlvo[i].foiDescoberta) {
            fprintf(arquivoSaida, "%s:%s\n", usuariosAlvo[i].login, usuariosAlvo[i].senhaDescoberta);
        } else {
            fprintf(arquivoSaida, "%s:NOT_FOUND\n", usuariosAlvo[i].login);
        }
    }
    fclose(arquivoSaida);
    
    // LIMPEZA FINAL
    // to liberando tudo que alocou, cada malloc precisa de um free correspondente.
    free(vetorThreads);
    free(argumentosThreads);
    free(usuariosAlvo);

    return 0; 
}
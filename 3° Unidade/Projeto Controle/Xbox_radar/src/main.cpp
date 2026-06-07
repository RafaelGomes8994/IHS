// =============================================================================
// main.cpp — Ponto de entrada do firmware do Rover FPV
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// Arquitetura Dual-Core (FreeRTOS sobre Arduino/ESP-IDF):
//
//   ┌─ Core 0 (APP_CPU) ─────────────────────────────────────────────────┐
//   │  TaskXbox   → BLE scan/connect/subscribe (driver_xbox)             │
//   │  [interno]  → Pilha Wi-Fi (gerenciada pelo ESP-IDF)                │
//   └────────────────────────────────────────────────────────────────────┘
//
//   ┌─ Core 1 (PRO_CPU) ─────────────────────────────────────────────────┐
//   │  TaskMotores → Consome filaComandos → aciona ponte H               │
//   │  loop()      → Escuta conexões TCP → produz filaComandos           │
//   └────────────────────────────────────────────────────────────────────┘
//
//   Fila compartilhada (thread-safe por design do FreeRTOS):
//   filaComandos ← [TCP] ← driver_tcp
//   filaComandos ← [BLE] ← driver_xbox
//   filaComandos → [Motores] ← TaskMotores
//
// =============================================================================

#include <Arduino.h>
#include "driver_motores.h"   // Direcao, filaComandos (extern), TaskMotores
#include "driver_tcp.h"       // inicializarWiFi, iniciarServidorTCP, verificarConexoesTCP
#include "driver_xbox.h"      // inicializarBLE, TaskXbox

// =============================================================================
// DEFINIÇÃO da fila de comandos compartilhada
// Todos os outros módulos a referenciam via: extern QueueHandle_t filaComandos;
// (declarado em driver_motores.h)
// =============================================================================
QueueHandle_t filaComandos = nullptr;

// =============================================================================
// Credenciais Wi-Fi — substitua pelos dados da sua rede
// =============================================================================
static const char* WIFI_SSID     = "Yorkshire012@5ghz";
static const char* WIFI_PASSWORD = "Rafael@12";

// =============================================================================
// setup() — executado uma vez no Core 1 na inicialização
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500); // Aguarda a serial estabilizar
    Serial.println("\n[MAIN] Rover FPV iniciando...");

    // -------------------------------------------------------------------------
    // 1. Cria a fila de comandos ANTES de qualquer módulo que a use
    //    Capacidade: 10 itens do tipo Direcao
    // -------------------------------------------------------------------------
    filaComandos = xQueueCreate(10, sizeof(Direcao));
    if (filaComandos == nullptr) {
        Serial.println("[MAIN] ERRO FATAL: Não foi possível criar filaComandos!");
        while (true) { delay(1000); } // Trava — sem fila o sistema é inoperável
    }
    Serial.println("[MAIN] Fila de comandos criada (capacidade: 10).");

    // -------------------------------------------------------------------------
    // 2. Inicializa os periféricos
    // -------------------------------------------------------------------------
    inicializarMotores();
    inicializarBLE();
    inicializarWiFi(WIFI_SSID, WIFI_PASSWORD);
    iniciarServidorTCP();

    // -------------------------------------------------------------------------
    // 3. Cria as Tasks do FreeRTOS
    //
    //    xTaskCreatePinnedToCore(funcao, nome, stackBytes, params, prioridade, handle, core)
    //    Prioridade: 0=mínima, configMAX_PRIORITIES-1=máxima
    //    Stack mínima para BLE: 8192 bytes (callbacks e scanning são pesados)
    // -------------------------------------------------------------------------

    // Task dos Motores — Core 1, alta prioridade para resposta determinística
    xTaskCreatePinnedToCore(
        TaskMotores,    // Função da task
        "Motores",      // Nome (debug)
        2048,           // Stack em bytes
        NULL,           // Parâmetros
        2,              // Prioridade (acima do loop)
        NULL,           // Handle (não precisamos guardar)
        1               // Core 1
    );

    // Task do Xbox BLE — Core 0, mesma prioridade (BLE usa seu próprio rádio)
    xTaskCreatePinnedToCore(
        TaskXbox,       // Função da task
        "XboxBLE",      // Nome (debug)
        8192,           // Stack generosa: BLE scan e callbacks são stack-heavy
        NULL,
        1,              // Prioridade 1 (abaixo dos motores)
        NULL,
        0               // Core 0 — junto com a pilha Wi-Fi
    );

    Serial.println("[MAIN] Tasks criadas. Sistema operacional.");
}

// =============================================================================
// loop() — executa continuamente no Core 1
// Responsabilidade: escutar conexões TCP e colocar comandos na fila.
// A velocidade do loop() determina a latência da interface TCP.
// =============================================================================
void loop() {
    verificarConexoesTCP();
    delay(10); // Cede CPU ao FreeRTOS por 10ms (libera para outras tasks)
}
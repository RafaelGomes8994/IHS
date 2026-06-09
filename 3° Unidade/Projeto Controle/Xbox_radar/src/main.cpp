// =============================================================================
// main.cpp — Ponto de entrada do firmware do Rover FPV
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// Arquitetura Dual-Core (FreeRTOS sobre Arduino/ESP-IDF):
//
//   CORE 0 (APP_CPU)
//   ├── TaskXbox    → BLE connect/subscribe (driver_xbox)
//   ├── TaskCamera  → OV2640 + servidor HTTP/MJPEG (camera_server)
//   └── [interno]  → Pilha Wi-Fi (ESP-IDF)
//
//   CORE 1 (PRO_CPU)
//   ├── TaskMotores → Consome filaComandos → ponte H (driver_motores)
//   └── loop()      → Escuta TCP → produz filaComandos (driver_tcp)
// =============================================================================

#include <Arduino.h>
#include "driver_motores.h"
#include "driver_tcp.h"
#include "driver_xbox.h"
#include "camera_server.h"

QueueHandle_t filaComandos = nullptr;

static const char* WIFI_SSID     = "Yorkshire012@5ghz";
static const char* WIFI_PASSWORD = "Rafael@12";

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[MAIN] Rover FPV iniciando...");

    filaComandos = xQueueCreate(10, sizeof(Direcao));
    if (filaComandos == nullptr) {
        Serial.println("[MAIN] ERRO FATAL: falha ao criar filaComandos!");
        while (true) { delay(1000); }
    }
    Serial.println("[MAIN] Fila criada (cap: 10 comandos).");

    inicializarMotores();
    inicializarBLE();
    inicializarWiFi(WIFI_SSID, WIFI_PASSWORD);
    iniciarServidorTCP();

    xTaskCreatePinnedToCore(TaskMotores, "Motores", 2048, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(TaskXbox,   "XboxBLE", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(TaskCamera, "Camera",  4096, NULL, 1, NULL, 0);

    Serial.println("[MAIN] Todas as tasks criadas. Sistema operacional.");
}

void loop() {
    verificarConexoesTCP();
    delay(10);
}
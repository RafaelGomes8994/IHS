#include <Arduino.h>
#include "driver_motores.h"
#include "driver_tcp.h"
#include "driver_xbox.h"

QueueHandle_t filaComandos = nullptr;

static const char* WIFI_SSID     = "Yorkshire012@2_4hz";
static const char* WIFI_PASSWORD = "Rafael@12";

void setup() {
    Serial.begin(115200);
    delay(500);



    Serial.println("\n[MAIN] Rover FPV iniciando...");

    // 1. Cria a fila (o Cérebro)
    filaComandos = xQueueCreate(10, sizeof(Direcao));

    // 2. Inicializa os pinos físicos
    inicializarMotores();

    // 3. LIGA O WI-FI PRIMEIRO (Garante o IP antes de ligar o rádio BLE)
    Serial.println("[MAIN] Iniciando Wi-Fi...");
    inicializarWiFi(WIFI_SSID, WIFI_PASSWORD);
    iniciarServidorTCP();

    // 4. LIGA O BLUETOOTH POR ÚLTIMO
    Serial.println("[MAIN] Iniciando Bluetooth...");
    inicializarBLE(); 

    // 5. Inicia as Tasks do FreeRTOS
    xTaskCreatePinnedToCore(TaskMotores, "Motores", 2048, NULL, 2, NULL, 1);
    
    // A Task do Xbox agora roda em background, sem travar o main
    xTaskCreatePinnedToCore(TaskXbox, "XboxBLE", 8192, NULL, 1, NULL, 0);
}

void loop() {
    verificarConexoesTCP();
    delay(10);
}
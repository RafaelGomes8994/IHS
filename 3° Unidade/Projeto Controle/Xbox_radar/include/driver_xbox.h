// =============================================================================
// driver_xbox.h — Driver BLE/HID para Controle Xbox Series S
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// Arquitetura:
//   Este driver implementa um cliente BLE (Central) que escaneia, conecta
//   e subscreve às notificações HID do controle. A Task TaskXbox() opera
//   numa máquina de estados (IDLE → SCAN → CONNECT → CONNECTED) no Core 0,
//   isolando o rádio BLE do Core 1 (Motores + TCP).
//
//   O mapeamento de bytes do HID Report foi obtido por engenharia reversa
//   e validado com MODO_SNIFFING=1. Desabilite o sniffing após validação.
// =============================================================================

#ifndef DRIVER_XBOX_H
#define DRIVER_XBOX_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>

// =============================================================================
// CONFIGURAÇÃO — Ajuste antes de compilar
// =============================================================================

// [1] Modo de sniffing: imprime payload bruto no Serial a cada notificação.
//     Use '1' para fazer engenharia reversa do mapa de bytes.
//     Use '0' após validar a XboxHIDReport_t.
#define MODO_SNIFFING 1

// [2] Limiar do joystick analógico para disparar um comando (0 a 32767).
//     8192 = 25% da excursão. Aumente para exigir movimento mais brusco.
#define LIMIAR_JOYSTICK 8192

// [3] Tempo máximo de scan antes de reiniciar a busca (ms).
#define TEMPO_SCAN_MS 5000

// =============================================================================
// UUIDs do Perfil HID BLE (Bluetooth SIG — padrão)
// =============================================================================
#define UUID_SERVICO_HID         "00001812-0000-1000-8000-00805f9b34fb"
#define UUID_CARACTERISTICA_HID  "00002a4d-0000-1000-8000-00805f9b34fb"

// Nome exato que aparece no advertising do Xbox Series S.
// Confirme com um app BLE scanner (ex: nRF Connect) se necessário.
#define NOME_DISPOSITIVO_XBOX    "Xbox Wireless Controller"

// =============================================================================
// Estrutura do HID Report (Report ID 0x01) — Xbox Series S via BLE
//
// Tamanho total: 16 bytes (sem o report_id) = 17 bytes com report_id.
//
// ATENÇÃO: Este mapa é baseado em engenharia reversa documentada pela
// comunidade. Valide SEMPRE com MODO_SNIFFING=1 antes de usar em produção.
// Mova cada eixo/botão isoladamente e observe qual byte/bit muda no Serial.
// =============================================================================
typedef struct __attribute__((packed)) {
    // -------------------------------------------------------------------------
    // Byte 0 — Report ID (sempre 0x01 para o relatório principal de gamepad)
    // -------------------------------------------------------------------------
    uint8_t  report_id;

    // -------------------------------------------------------------------------
    // Bytes 1–2 — Bitmask de botões (little-endian uint16)
    // Usar as constantes BTN_x definidas abaixo.
    // -------------------------------------------------------------------------
    uint16_t botoes;

    // -------------------------------------------------------------------------
    // Bytes 3–4 — Gatilho esquerdo (uint16, 0 = solto, 1023 = pressionado)
    // Bytes 5–6 — Gatilho direito
    // -------------------------------------------------------------------------
    uint16_t gatilho_esq;
    uint16_t gatilho_dir;

    // -------------------------------------------------------------------------
    // Bytes 7–8  — Joystick esquerdo X (int16, -32768 ← centro=0 → +32767)
    // Bytes 9–10 — Joystick esquerdo Y (int16, ↑ = negativo / ↓ = positivo)
    // Bytes 11–12 — Joystick direito X
    // Bytes 13–14 — Joystick direito Y
    // -------------------------------------------------------------------------
    int16_t  joystick_esq_x;
    int16_t  joystick_esq_y;
    int16_t  joystick_dir_x;
    int16_t  joystick_dir_y;

    // -------------------------------------------------------------------------
    // Byte 15 — D-Pad (hat switch)
    // 0=↑  1=↗  2=→  3=↘  4=↓  5=↙  6=←  7=↖  8=Neutro
    // -------------------------------------------------------------------------
    uint8_t  dpad;

} XboxHIDReport_t;

// =============================================================================
// Bitmask dos botões (campo 'botoes' da XboxHIDReport_t)
// =============================================================================
#define BTN_A      (1 << 0)   // Botão A (verde)
#define BTN_B      (1 << 1)   // Botão B (vermelho)
#define BTN_SHARE  (1 << 2)   // Botão Share/Captura
#define BTN_X      (1 << 3)   // Botão X (azul)
#define BTN_Y      (1 << 4)   // Botão Y (amarelo)
#define BTN_LB     (1 << 5)   // Bumper esquerdo
#define BTN_RB     (1 << 6)   // Bumper direito
#define BTN_VIEW   (1 << 9)   // Botão View (antigo Back)
#define BTN_MENU   (1 << 10)  // Botão Menu (antigo Start)
#define BTN_LS     (1 << 11)  // Click do joystick esquerdo
#define BTN_RS     (1 << 12)  // Click do joystick direito

// =============================================================================
// Estados internos da máquina de estados do driver (uso interno, não expor)
// =============================================================================
typedef enum {
    XBOX_IDLE,          // Aguardando início
    XBOX_ESCANEANDO,    // BLE scan ativo
    XBOX_CONECTANDO,    // Handshake BLE em andamento
    XBOX_CONECTADO,     // Subscrito às notificações HID
    XBOX_DESCONECTADO   // Conexão perdida — vai reiniciar o scan
} EstadoBLE_t;

// =============================================================================
// Protótipos públicos
// =============================================================================

// Inicializa a pilha BLE do ESP32. Chame ANTES de xTaskCreate(TaskXbox).
void inicializarBLE();

// Task FreeRTOS — máquina de estados BLE (scan → connect → subscribe).
// Fixar no Core 0 com xTaskCreatePinnedToCore(..., 0).
void TaskXbox(void* parametros);

// Processa o payload bruto recebido pelo callback de notificação HID.
// Chamada internamente pelo callback — não chamar diretamente no main.
void processarDadosXbox(uint8_t* dados, size_t tamanho);

#endif // DRIVER_XBOX_H
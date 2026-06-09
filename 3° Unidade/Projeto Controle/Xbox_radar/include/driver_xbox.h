// =============================================================================
// driver_xbox.h — Driver BLE/HID para Controle Xbox Series S
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
// =============================================================================

#ifndef DRIVER_XBOX_H
#define DRIVER_XBOX_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEAddress.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>

// =============================================================================
// MAC address do controle Xbox Series S (obtido via Serial/nRF Connect)
// Conexão direta por MAC — elimina o scan BLE (mais rápido e confiável)
// =============================================================================
#define MAC_XBOX "14:cb:65:8c:9c:55"

// =============================================================================
// UUIDs do Perfil HID BLE (Bluetooth SIG — padrão)
// =============================================================================
#define UUID_SERVICO_HID        "00001812-0000-1000-8000-00805f9b34fb"
#define UUID_CARACTERISTICA_HID "00002a4d-0000-1000-8000-00805f9b34fb"

// =============================================================================
// Configuração
// =============================================================================

// Modo sniffing: 1 = imprime payload bruto no Serial (engenharia reversa)
//               0 = decodifica e envia para fila (operação normal)
#define MODO_SNIFFING 1

// Limiar do joystick (25% da excursão total de 32767)
#define LIMIAR_JOYSTICK 8192

// Intervalo entre tentativas de reconexão (ms)
#define INTERVALO_RECONEXAO_MS 3000

// =============================================================================
// Estrutura do HID Report (Report ID 0x01) — Xbox Series S via BLE
// Validar mapa com MODO_SNIFFING=1 antes de usar em producao.
// =============================================================================
typedef struct __attribute__((packed)) {
    uint8_t  report_id;       // Byte 0:  Sempre 0x01
    uint16_t botoes;          // Bytes 1-2: Bitmask de botões
    uint16_t gatilho_esq;     // Bytes 3-4: Gatilho esq. (0-1023)
    uint16_t gatilho_dir;     // Bytes 5-6: Gatilho dir. (0-1023)
    int16_t  joystick_esq_x;  // Bytes 7-8: Analógico esq. X (-32768 a +32767)
    int16_t  joystick_esq_y;  // Bytes 9-10: Analógico esq. Y (↑ = negativo)
    int16_t  joystick_dir_x;  // Bytes 11-12: Analógico dir. X
    int16_t  joystick_dir_y;  // Bytes 13-14: Analógico dir. Y
    uint8_t  dpad;            // Byte 15: D-pad (0=↑ 2=→ 4=↓ 6=← 8=neutro)
} XboxHIDReport_t;

// =============================================================================
// Bitmask dos botões (campo 'botoes' da XboxHIDReport_t)
// =============================================================================
#define BTN_A      (1 << 0)
#define BTN_B      (1 << 1)
#define BTN_SHARE  (1 << 2)
#define BTN_X      (1 << 3)
#define BTN_Y      (1 << 4)
#define BTN_LB     (1 << 5)
#define BTN_RB     (1 << 6)
#define BTN_VIEW   (1 << 9)
#define BTN_MENU   (1 << 10)
#define BTN_LS     (1 << 11)
#define BTN_RS     (1 << 12)

// =============================================================================
// Estados da máquina de estados BLE
// =============================================================================
typedef enum {
    XBOX_CONECTANDO,    // Tentando estabelecer conexão BLE
    XBOX_CONECTADO,     // Subscrito às notificações HID
    XBOX_DESCONECTADO   // Conexão perdida — vai reconectar
} EstadoBLE_t;

// =============================================================================
// Protótipos públicos
// =============================================================================
void inicializarBLE();
void TaskXbox(void* parametros);
void processarDadosXbox(uint8_t* dados, size_t tamanho);

#endif // DRIVER_XBOX_H
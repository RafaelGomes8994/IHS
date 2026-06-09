// =============================================================================
// driver_xbox.cpp — Driver BLE/HID para Controle Xbox Series S
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// Fluxo (conexão direta por MAC — sem scan):
//
//   inicializarBLE()
//        ↓
//   TaskXbox() — Core 0
//        ↓
//   [CONECTANDO] BLEClient::connect(MAC_XBOX)
//        ↓
//   Localiza Serviço HID (0x1812)
//   Itera características (0x2A4D) → seleciona a que tem canNotify()
//   Subscreve → registerForNotify(notificacaoHID)
//        ↓
//   [CONECTADO] notificacaoHID() dispara a cada frame do controle
//               → processarDadosXbox() → xQueueSend(filaComandos)
//        ↓
//   [DESCONECTADO] onDisconnect() → aguarda → volta para CONECTANDO
// =============================================================================

#include "driver_xbox.h"
#include "driver_motores.h"

// =============================================================================
// Estado compartilhado entre callbacks e Task
// volatile: impede que o compilador cache o valor em registrador
// =============================================================================
static volatile EstadoBLE_t estadoAtual = XBOX_CONECTANDO;

static BLEClient*              clienteBLE        = nullptr;
static BLERemoteCharacteristic* caracteristicaHID = nullptr;

// =============================================================================
// Callback de notificação HID — chamado a cada frame do controle (~60Hz)
// Contexto: task interna do BLE stack (não bloquear aqui)
// =============================================================================
static void notificacaoHID(BLERemoteCharacteristic* pCarac,
                            uint8_t* dados,
                            size_t   tamanho,
                            bool     eNotificacao) {
    if (!eNotificacao || dados == nullptr || tamanho == 0) return;
    processarDadosXbox(dados, tamanho);
}

// =============================================================================
// Callback de eventos de conexão/desconexão
// =============================================================================
class CallbackClienteXbox : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        Serial.println("[XBOX BLE] Conectado!");
    }
    void onDisconnect(BLEClient* pClient) override {
        Serial.println("[XBOX BLE] Desconectado. Reconectando em breve...");
        estadoAtual = XBOX_DESCONECTADO;
    }
};

// =============================================================================
// conectarESubscrever()
// Conecta diretamente ao MAC_XBOX e subscreve ao input report HID.
// Retorna true se todo o fluxo for bem-sucedido.
// =============================================================================
static bool conectarESubscrever() {
    BLEAddress enderecoXbox(MAC_XBOX);

    // --- 1. Cria e configura o cliente ---
    clienteBLE = BLEDevice::createClient();
    clienteBLE->setClientCallbacks(new CallbackClienteXbox());

    Serial.printf("[XBOX BLE] Conectando ao MAC %s...\n", MAC_XBOX);

    // Conexão direta por MAC — sem scan, sem espera de advertising
    if (!clienteBLE->connect(enderecoXbox)) {
        Serial.println("[XBOX BLE] Falha na conexão. Controle está ligado e em modo pareamento?");
        return false;
    }

    // --- 2. Localiza o Serviço HID (0x1812) ---
    BLERemoteService* servicoHID = clienteBLE->getService(BLEUUID(UUID_SERVICO_HID));
    if (servicoHID == nullptr) {
        Serial.println("[XBOX BLE] Serviço HID (0x1812) não encontrado.");
        clienteBLE->disconnect();
        return false;
    }
    Serial.println("[XBOX BLE] Serviço HID encontrado.");

    // --- 3. Itera as características HID Report (0x2A4D) ---
    // O controle expõe várias (input, output, feature reports).
    // Precisamos da que tem canNotify() = input report (dados do joystick).
    auto* mapaCarac = servicoHID->getCharacteristics();
    caracteristicaHID = nullptr;

    for (auto& par : *mapaCarac) {
        BLERemoteCharacteristic* carac = par.second;
        if (carac->getUUID().equals(BLEUUID(UUID_CARACTERISTICA_HID)) &&
            carac->canNotify()) {
            caracteristicaHID = carac;
            Serial.printf("[XBOX BLE] Input Report encontrado. Handle: 0x%04X\n",
                          carac->getHandle());
            break;
        }
    }

    if (caracteristicaHID == nullptr) {
        Serial.println("[XBOX BLE] Nenhuma característica HID com Notify.");
        clienteBLE->disconnect();
        return false;
    }

    // --- 4. Subscreve às notificações ---
    caracteristicaHID->registerForNotify(notificacaoHID);
    Serial.println("[XBOX BLE] Subscrito. Controle ativo!");
    return true;
}

// =============================================================================
// inicializarBLE()
// =============================================================================
void inicializarBLE() {
    BLEDevice::init("Rover-ESP32");
    Serial.println("[XBOX BLE] Pilha BLE inicializada.");
}

// =============================================================================
// TaskXbox() — Máquina de estados BLE, Core 0, stack 8192 bytes
// =============================================================================
void TaskXbox(void* parametros) {
    estadoAtual = XBOX_CONECTANDO;

    for (;;) {
        switch (estadoAtual) {

            // -----------------------------------------------------------------
            case XBOX_CONECTANDO:
                if (conectarESubscrever()) {
                    estadoAtual = XBOX_CONECTADO;
                } else {
                    // Libera recursos e tenta novamente
                    clienteBLE        = nullptr;
                    caracteristicaHID = nullptr;
                    Serial.printf("[XBOX BLE] Tentando novamente em %dms...\n",
                                  INTERVALO_RECONEXAO_MS);
                    vTaskDelay(pdMS_TO_TICKS(INTERVALO_RECONEXAO_MS));
                }
                break;

            // -----------------------------------------------------------------
            case XBOX_CONECTADO:
                // Task suspensa: tudo acontece no callback notificacaoHID()
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            // -----------------------------------------------------------------
            case XBOX_DESCONECTADO:
                clienteBLE        = nullptr;
                caracteristicaHID = nullptr;
                vTaskDelay(pdMS_TO_TICKS(INTERVALO_RECONEXAO_MS));
                estadoAtual = XBOX_CONECTANDO;
                break;
        }
    }
}

// =============================================================================
// processarDadosXbox()
// =============================================================================
void processarDadosXbox(uint8_t* dados, size_t tamanho) {

#if MODO_SNIFFING == 1
    // -------------------------------------------------------------------------
    // Protocolo de engenharia reversa:
    //   1. Segure o controle parado → observe a linha base
    //   2. Empurre o analógico esq. para CIMA → veja qual byte muda
    //   3. Empurre para BAIXO → confirme que o mesmo byte muda em sentido oposto
    //   4. Repita para cada eixo/botão e documente na XboxHIDReport_t
    // -------------------------------------------------------------------------
    Serial.printf("[SNIFF %02d bytes] [ ", tamanho);
    for (size_t i = 0; i < tamanho; i++) {
        Serial.printf("%02X ", dados[i]);
    }
    Serial.println("]");
#endif

    if (tamanho < 15) return; // payload muito curto (outro tipo de report)

    // Detecta se o report_id (0x01) está incluso no buffer
    uint8_t offset = (dados[0] == 0x01) ? 1 : 0;

    // Lê o joystick esquerdo Y (int16 little-endian)
    // Índice 8-9 relativo ao offset — VALIDE com o sniffing acima
    if (tamanho < (size_t)(offset + 10)) return;

    int16_t joystick_y = (int16_t)(dados[offset + 8] |
                                   (dados[offset + 9] << 8));

    Direcao cmd = PARAR;
    if      (joystick_y < -LIMIAR_JOYSTICK) cmd = FRENTE;
    else if (joystick_y >  LIMIAR_JOYSTICK) cmd = TRAS;

    if (filaComandos != nullptr) {
        xQueueSend(filaComandos, &cmd, 0);
    }
}
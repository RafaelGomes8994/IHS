// =============================================================================
// driver_xbox.cpp — Driver BLE/HID para Controle Xbox Series S
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
// =============================================================================

#include "driver_xbox.h"
#include "driver_motores.h"

static volatile EstadoBLE_t estadoAtual = XBOX_CONECTANDO;
static volatile bool segurancaEstabelecida = false;

static BLEClient* clienteBLE        = nullptr;
static BLERemoteCharacteristic* caracteristicaHID = nullptr;

extern QueueHandle_t filaComandos; // Garantindo o acesso à fila

// =============================================================================
// Callback de segurança BLE
// =============================================================================
class CallbackSeguranca : public BLESecurityCallbacks {

    bool onConfirmPIN(uint32_t pin) override {
        return true;
    }

    uint32_t onPassKeyRequest() override {
        return 0;
    }

    void onPassKeyNotify(uint32_t pass_key) override {}

    bool onSecurityRequest() override {
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) override {
        if (auth_cmpl.success) {
            Serial.println("[XBOX BLE] Bonding concluído — controle autenticado!");
            segurancaEstabelecida = true;
        } else {
            Serial.printf("[XBOX BLE] Falha na autenticação. Código: 0x%02X\n",
                          auth_cmpl.fail_reason);
            segurancaEstabelecida = false;
        }
    }
};

// =============================================================================
// processarDadosXbox() - AGORA LENDO OS BOTÕES X, Y e B
// =============================================================================
void processarDadosXbox(uint8_t* dados, size_t tamanho) {

#if MODO_SNIFFING == 1
    Serial.printf("[SNIFF %02d bytes] [ ", tamanho);
    for (size_t i = 0; i < tamanho; i++) {
        Serial.printf("%02X ", dados[i]);
    }
    Serial.println("]");
#endif

    // Tamanho mínimo para garantir que temos os bytes dos botões
    if (tamanho < 15) return;

    uint8_t offset = (dados[0] == 0x01) ? 1 : 0;
    
    // Evita crash de ponteiro lendo fora do array
    if (tamanho < (size_t)(offset + 15)) return;

    // Extração da Máscara de Botões (O "Byte Mágico" no índice 14)
    uint8_t maskBotoes = dados[offset + 14];

    Direcao cmd = PARAR;

    // Decodificação via Bitwise AND (0x08 = X, 0x10 = Y, 0x02 = B)
    if (maskBotoes & 0x08) {         
        cmd = FRENTE;
        Serial.println("[XBOX] Botao X pressionado -> FRENTE");
    } 
    else if (maskBotoes & 0x10) {    
        cmd = TRAS;
        Serial.println("[XBOX] Botao Y pressionado -> TRAS");
    }
    else if (maskBotoes & 0x02) {    
        cmd = PARAR;
        Serial.println("[XBOX] Botao B pressionado -> FREIO");
    }

    // Envia o comando para a fila (apenas se a fila existir)
    if (filaComandos != nullptr) {
        xQueueSend(filaComandos, &cmd, 0);
    }
}

// =============================================================================
// Callback de notificação HID
// =============================================================================
static void notificacaoHID(BLERemoteCharacteristic* pCarac,
                            uint8_t* dados,
                            size_t   tamanho,
                            bool     eNotificacao) {
    if (!eNotificacao || dados == nullptr || tamanho == 0) return;
    processarDadosXbox(dados, tamanho);
}

// =============================================================================
// Callback de conexão/desconexão
// =============================================================================
class CallbackClienteXbox : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        Serial.println("[XBOX BLE] Conectado!");
    }
    void onDisconnect(BLEClient* pClient) override {
        Serial.println("[XBOX BLE] Desconectado. Reconectando...");
        segurancaEstabelecida = false;
        estadoAtual = XBOX_DESCONECTADO;
    }
};

// =============================================================================
// conectarESubscrever()
// =============================================================================
static bool conectarESubscrever() {
    BLEAddress enderecoXbox(MAC_XBOX);

    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    clienteBLE = BLEDevice::createClient();
    clienteBLE->setClientCallbacks(new CallbackClienteXbox());

    Serial.printf("[XBOX BLE] Conectando ao MAC %s...\n", MAC_XBOX);
    if (!clienteBLE->connect(enderecoXbox)) {
        Serial.println("[XBOX BLE] Falha na conexão.");
        return false;
    }

    Serial.println("[XBOX BLE] Aguardando autenticação BLE...");
    int espera = 0;
    while (!segurancaEstabelecida && espera < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        espera++;
    }

    if (!segurancaEstabelecida) {
        Serial.println("[XBOX BLE] Timeout na autenticação. Tentando continuar...");
    }

    BLERemoteService* servicoHID = clienteBLE->getService(BLEUUID(UUID_SERVICO_HID));
    if (servicoHID == nullptr) {
        Serial.println("[XBOX BLE] Serviço HID não encontrado.");
        clienteBLE->disconnect();
        return false;
    }
    Serial.println("[XBOX BLE] Serviço HID encontrado.");

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
        Serial.println("[XBOX BLE] Característica HID com Notify não encontrada.");
        clienteBLE->disconnect();
        return false;
    }

    caracteristicaHID->registerForNotify(notificacaoHID);
    Serial.println("[XBOX BLE] Subscrito. Controle ativo!");
    return true;
}

// =============================================================================
// inicializarBLE()
// =============================================================================
void inicializarBLE() {
    BLEDevice::init("Rover-ESP32");

    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(new CallbackSeguranca());

    Serial.println("[XBOX BLE] Pilha BLE inicializada.");
}

// =============================================================================
// TaskXbox()
// =============================================================================
void TaskXbox(void* parametros) {
    estadoAtual = XBOX_CONECTANDO;

    for (;;) {
        switch (estadoAtual) {

            case XBOX_CONECTANDO:
                segurancaEstabelecida = false;
                if (conectarESubscrever()) {
                    estadoAtual = XBOX_CONECTADO;
                } else {
                    clienteBLE        = nullptr;
                    caracteristicaHID = nullptr;
                    // INTERVALO_RECONEXAO_MS (suponho que seja 3000ms no seu .h)
                    vTaskDelay(pdMS_TO_TICKS(3000)); 
                }
                break;

            case XBOX_CONECTADO:
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            case XBOX_DESCONECTADO:
                clienteBLE        = nullptr;
                caracteristicaHID = nullptr;
                vTaskDelay(pdMS_TO_TICKS(3000));
                estadoAtual = XBOX_CONECTANDO;
                break;
        }
    }
}
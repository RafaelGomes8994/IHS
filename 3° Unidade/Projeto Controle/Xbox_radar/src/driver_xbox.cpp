// =============================================================================
// driver_xbox.cpp — Implementação do Driver BLE/HID para Xbox Series S
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// Fluxo de operação:
//
//   inicializarBLE()
//        ↓
//   TaskXbox() — roda permanentemente no Core 0
//        ↓
//   [SCAN] BLEScan procura "Xbox Wireless Controller"
//        ↓
//   [CONNECT] BLEClient::connect()
//        ↓
//   [SUBSCRIBE] Localiza todas as características HID Report (UUID 0x2A4D)
//               e subscreve à que suporta Notify (o input report).
//        ↓
//   [CONNECTED] Callback notificacaoHID() é disparado a cada frame do controle
//               → processarDadosXbox() → xQueueSend(filaComandos)
//        ↓
//   [DESCONECTADO] clientCallback::onDisconnect() sinaliza → volta ao SCAN
// =============================================================================

#include "driver_xbox.h"
#include "driver_motores.h"    // Precisa de: Direcao, filaComandos

// =============================================================================
// Variável de estado — compartilhada entre callbacks e a Task
// =============================================================================
static volatile EstadoBLE_t estadoAtual = XBOX_IDLE;

// Ponteiro para o dispositivo encontrado no scan
static BLEAdvertisedDevice* dispositivoEncontrado = nullptr;

// Handle do cliente BLE ativo
static BLEClient* clienteBLE = nullptr;

// Characterística HID para leitura/notificação
static BLERemoteCharacteristic* caracteristicaHID = nullptr;

// =============================================================================
// Callback de notificação — disparado a cada HID Report recebido
// Roda dentro do contexto de tarefa BLE do ESP-IDF (não bloquear aqui).
// =============================================================================
static void notificacaoHID(BLERemoteCharacteristic* pCarac,
                            uint8_t* dados,
                            size_t   tamanho,
                            bool     eNotificacao) {
    if (!eNotificacao || dados == nullptr || tamanho == 0) return;
    processarDadosXbox(dados, tamanho);
}

// =============================================================================
// Callback de eventos de conexão/desconexão do cliente
// =============================================================================
class CallbackClienteXbox : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        Serial.println("[XBOX BLE] Conectado ao controle!");
    }

    void onDisconnect(BLEClient* pClient) override {
        Serial.println("[XBOX BLE] Controle desconectado. Reiniciando scan...");
        estadoAtual = XBOX_DESCONECTADO;
    }
};

// =============================================================================
// Callback de scan — disparado para cada dispositivo BLE encontrado
// =============================================================================
class CallbackScanXbox : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice anuncio) override {
        // Filtra pelo nome exato do controle
        if (anuncio.haveName() &&
            anuncio.getName() == NOME_DISPOSITIVO_XBOX) {

            Serial.printf("[XBOX BLE] Controle encontrado! MAC: %s\n",
                          anuncio.getAddress().toString().c_str());

            // Para o scan e guarda o dispositivo para conexão
            BLEDevice::getScan()->stop();
            dispositivoEncontrado = new BLEAdvertisedDevice(anuncio);
            estadoAtual = XBOX_CONECTANDO;
        }
    }
};

// =============================================================================
// inicializarBLE()
// Deve ser chamada no setup() ANTES de criar a TaskXbox.
// =============================================================================
void inicializarBLE() {
    BLEDevice::init("Rover-ESP32");
    Serial.println("[XBOX BLE] Pilha BLE inicializada.");
}

// =============================================================================
// conectarESubscrever()
// Tenta conectar ao dispositivo encontrado e subscreve ao HID Report.
// Retorna true se o fluxo completo for bem-sucedido.
// =============================================================================
static bool conectarESubscrever() {
    // --- 1. Cria e configura o cliente ---
    clienteBLE = BLEDevice::createClient();
    clienteBLE->setClientCallbacks(new CallbackClienteXbox());

    Serial.println("[XBOX BLE] Tentando conectar...");
    if (!clienteBLE->connect(dispositivoEncontrado)) {
        Serial.println("[XBOX BLE] ERRO: Falha na conexão BLE.");
        return false;
    }

    // --- 2. Localiza o Serviço HID (UUID 0x1812) ---
    BLERemoteService* servicoHID =
        clienteBLE->getService(BLEUUID(UUID_SERVICO_HID));

    if (servicoHID == nullptr) {
        Serial.println("[XBOX BLE] ERRO: Serviço HID não encontrado.");
        clienteBLE->disconnect();
        return false;
    }
    Serial.println("[XBOX BLE] Serviço HID (0x1812) encontrado.");

    // --- 3. Itera todas as características HID Report (UUID 0x2A4D) ---
    // O controle expõe múltiplas características com este UUID (input, output,
    // feature reports). Precisamos da que suporta Notify — o input report.
    auto mapaCaracteristicas =
        servicoHID->getCharacteristics();

    caracteristicaHID = nullptr;

    for (auto& par : *mapaCaracteristicas) {
        BLERemoteCharacteristic* carac = par.second;

        if (carac->getUUID().equals(BLEUUID(UUID_CARACTERISTICA_HID))) {

            // Verifica se esta característica suporta notificações (input report)
            if (carac->canNotify()) {
                caracteristicaHID = carac;
                Serial.printf("[XBOX BLE] Input Report encontrado. Handle: 0x%04X\n",
                              carac->getHandle());
                break;
            }
        }
    }

    if (caracteristicaHID == nullptr) {
        Serial.println("[XBOX BLE] ERRO: Nenhuma característica HID com Notify.");
        clienteBLE->disconnect();
        return false;
    }

    // --- 4. Subscreve às notificações ---
    caracteristicaHID->registerForNotify(notificacaoHID);
    Serial.println("[XBOX BLE] Subscrito às notificações HID. Controle ativo!");

    return true;
}

// =============================================================================
// TaskXbox() — Máquina de estados BLE, fixar no Core 0
// =============================================================================
void TaskXbox(void* parametros) {
    BLEScan* scanner = BLEDevice::getScan();
    scanner->setAdvertisedDeviceCallbacks(new CallbackScanXbox());
    scanner->setActiveScan(true);   // Ativo = solicita dados extras no scan
    scanner->setInterval(100);
    scanner->setWindow(99);

    estadoAtual = XBOX_ESCANEANDO;

    for (;;) {
        switch (estadoAtual) {

            // -----------------------------------------------------------------
            case XBOX_ESCANEANDO:
                Serial.println("[XBOX BLE] Iniciando scan BLE...");
                dispositivoEncontrado = nullptr;
                // Scan bloqueante por TEMPO_SCAN_MS ms
                scanner->start(TEMPO_SCAN_MS / 1000, false);

                // Se após o scan nenhum dispositivo foi encontrado, repete
                if (estadoAtual == XBOX_ESCANEANDO) {
                    Serial.println("[XBOX BLE] Controle não encontrado. Tentando novamente...");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                break;

            // -----------------------------------------------------------------
            case XBOX_CONECTANDO:
                if (conectarESubscrever()) {
                    estadoAtual = XBOX_CONECTADO;
                } else {
                    // Falha na conexão — libera memória e volta ao scan
                    if (dispositivoEncontrado) {
                        delete dispositivoEncontrado;
                        dispositivoEncontrado = nullptr;
                    }
                    estadoAtual = XBOX_ESCANEANDO;
                    vTaskDelay(pdMS_TO_TICKS(2000));
                }
                break;

            // -----------------------------------------------------------------
            case XBOX_CONECTADO:
                // Neste estado a Task fica suspensa: todo o trabalho é feito
                // no callback de notificação (notificacaoHID) que roda
                // assincronamente quando o controle envia um novo frame.
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            // -----------------------------------------------------------------
            case XBOX_DESCONECTADO:
                // Limpa recursos e reinicia o ciclo
                if (clienteBLE != nullptr) {
                    if (clienteBLE->isConnected()) {
                        clienteBLE->disconnect();
                    }
                    // Nota: BLEDevice::deleteClient() não existe na lib padrão.
                    // O cliente é liberado pelo garbage collector interno do BLE stack.
                    clienteBLE = nullptr;
                }
                if (dispositivoEncontrado != nullptr) {
                    delete dispositivoEncontrado;
                    dispositivoEncontrado = nullptr;
                }
                caracteristicaHID = nullptr;
                vTaskDelay(pdMS_TO_TICKS(1000));
                estadoAtual = XBOX_ESCANEANDO;
                break;

            // -----------------------------------------------------------------
            default:
                estadoAtual = XBOX_ESCANEANDO;
                break;
        }
    }
}

// =============================================================================
// processarDadosXbox()
// Interpreta o payload HID bruto recebido do controle.
//
// Com MODO_SNIFFING=1:
//   Imprime todos os bytes no formato "[ B0 B1 B2 ... ]" no Serial.
//   Protocolo de engenharia reversa:
//     1. Compile e conecte com o controle.
//     2. Segure o controle parado → observe a linha base de bytes.
//     3. Empurre o analógico esquerdo para CIMA → observe qual byte muda.
//     4. Empurre para BAIXO → observe o mesmo byte mudar em direção oposta.
//     5. Repita para cada botão/eixo. Documente na XboxHIDReport_t.
//
// Com MODO_SNIFFING=0:
//   Usa a XboxHIDReport_t para decodificar os eixos e enviar para a fila.
// =============================================================================
void processarDadosXbox(uint8_t* dados, size_t tamanho) {

#if MODO_SNIFFING == 1
    // -------------------------------------------------------------------------
    // Modo Sniffing: dump completo de todos os bytes
    // -------------------------------------------------------------------------
    Serial.printf("[SNIFF %02d bytes] [ ", tamanho);
    for (size_t i = 0; i < tamanho; i++) {
        Serial.printf("%02X ", dados[i]);
    }
    Serial.println("]");

    // Mesmo em modo sniffing, tenta usar o mapa atual para movimentação
    // básica — útil para testar o rover enquanto valida os bytes.

#endif // MODO_SNIFFING

    // -------------------------------------------------------------------------
    // Decodificação do HID Report
    // -------------------------------------------------------------------------

    // Tamanho mínimo esperado: 16 bytes (sem report_id) ou 17 (com report_id).
    // O ESP32 BLE pode entregar o payload COM ou SEM o report_id dependendo
    // de como o Report Reference Descriptor está configurado no controle.
    // Ajuste o offset abaixo conforme o sniffing revelar.
    if (tamanho < 15) {
        // Payload muito curto — pode ser outro tipo de report (ex: bateria)
        return;
    }

    // Faz o cast do buffer para a struct (requer que o mapa esteja correto!)
    // ATENÇÃO: só remova o comentário abaixo após validar com MODO_SNIFFING=1
    // const XboxHIDReport_t* report = (const XboxHIDReport_t*) dados;

    // -------------------------------------------------------------------------
    // Leitura direta por índice de byte (mais segura durante a fase de sniffing)
    //
    // SUBSTITUA OS ÍNDICES ABAIXO PELOS CORRETOS APÓS O SNIFFING:
    //   - joystick_y_bruto: byte que muda ao mover o analógico esq. verticalmente
    //   - offset: 0 se o report_id (0x01) NÃO está incluído no buffer
    //             1 se o report_id (0x01) ESTÁ incluído no buffer
    // -------------------------------------------------------------------------

    // Detecta automaticamente se o report_id está no buffer
    uint8_t offset = (dados[0] == 0x01) ? 1 : 0;

    // Com base no mapa documentado (bytes 9-10 = joystick_esq_y, int16 LE):
    // Índice relativo ao offset: byte 8 (LSB) e byte 9 (MSB) do joystick_esq_y
    int16_t joystick_y_bruto = 0;

    if (tamanho >= (size_t)(offset + 10)) {
        // int16 little-endian: LSB no índice menor
        joystick_y_bruto = (int16_t)(dados[offset + 8] |
                                     (dados[offset + 9] << 8));
    }

    // Converte o valor do joystick em Direcao
    Direcao cmd = PARAR;

    if (joystick_y_bruto < -LIMIAR_JOYSTICK) {
        // Joystick empurrado para CIMA (valores negativos = frente do rover)
        cmd = FRENTE;
    } else if (joystick_y_bruto > LIMIAR_JOYSTICK) {
        // Joystick empurrado para BAIXO
        cmd = TRAS;
    }

    // Envia para a fila de comandos (consumida pela TaskMotores no Core 1)
    // Não bloqueia (timeout=0): se a fila estiver cheia, descarta o frame.
    if (filaComandos != nullptr) {
        xQueueSend(filaComandos, &cmd, 0);
    }
}
// =============================================================================
// driver_motores.cpp — Implementação do Driver de Motores DC
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
// =============================================================================

#include "driver_motores.h"

// =============================================================================
// Pinagem da Ponte H — ajuste conforme o seu cabeamento no ESP32-CAM
// Pinos disponíveis no ESP32-CAM (sem conflito com câmera e PSRAM):
//   GPIO 12, 13, 14, 15 — usados aqui
//   EVITE: GPIO 0 (boot), 1/3 (UART), 4 (flash LED), 2/33/34/35/36/39 (câmera)
// =============================================================================
static const int pinoMotor1_F = 12;   // Motor 1 — sentido Frente
static const int pinoMotor1_B = 13;   // Motor 1 — sentido Trás
static const int pinoMotor2_F = 14;   // Motor 2 — sentido Frente
static const int pinoMotor2_B = 15;   // Motor 2 — sentido Trás

// =============================================================================
// Função Assembly externa (calculo_pwm.S) — implementa a escala de velocidade
// Assinatura: uint32_t calcularPWM(uint8_t entrada)
// Entrada: 0-255 (proporção da velocidade desejada)
// Saída:   valor de duty cycle mapeado para o range do ledc (0-1023 ou 0-255)
// =============================================================================
extern "C" uint32_t calcularPWM(uint8_t entrada);

// Pré-calcula o PWM para velocidade máxima (entrada=200 ≈ 78% de 255)
static const uint32_t pwmVelocidade = calcularPWM(200);

// =============================================================================
// inicializarMotores()
// =============================================================================
void inicializarMotores() {
    pinMode(pinoMotor1_F, OUTPUT);
    pinMode(pinoMotor1_B, OUTPUT);
    pinMode(pinoMotor2_F, OUTPUT);
    pinMode(pinoMotor2_B, OUTPUT);

    // Garante que o robô começa parado (segurança)
    processarComandoMotor(PARAR);

    Serial.println("[MOTORES] Pinos configurados. Robô parado.");
}

// =============================================================================
// processarComandoMotor()
// Aciona os pinos da ponte H conforme a direção.
//
// Nota sobre o ledc (PWM de hardware do ESP32):
//   O canal 0 do ledc deve ser configurado no setup() com ledcSetup() e
//   ledcAttachPin() antes de usar ledcWrite(). Para simplificar e garantir
//   compatibilidade com o ESP32-CAM (onde o ledc compartilha timer com a
//   câmera), optou-se por usar digitalWrite() direto (velocidade máxima).
//   Para controle proporcional de velocidade, use ledcWrite() no canal
//   dedicado após configurar o timer no inicializarMotores().
// =============================================================================
void processarComandoMotor(Direcao cmd) {
    switch (cmd) {
        case FRENTE:
            // Ambos os motores giram para frente
            digitalWrite(pinoMotor1_F, HIGH); digitalWrite(pinoMotor1_B, LOW);
            digitalWrite(pinoMotor2_F, HIGH); digitalWrite(pinoMotor2_B, LOW);
            break;

        case TRAS:
            // Ambos os motores giram para trás
            digitalWrite(pinoMotor1_F, LOW);  digitalWrite(pinoMotor1_B, HIGH);
            digitalWrite(pinoMotor2_F, LOW);  digitalWrite(pinoMotor2_B, HIGH);
            break;

        case PARAR:
        default:
            // Freio elétrico: todos os pinos LOW
            digitalWrite(pinoMotor1_F, LOW);  digitalWrite(pinoMotor1_B, LOW);
            digitalWrite(pinoMotor2_F, LOW);  digitalWrite(pinoMotor2_B, LOW);
            break;
    }
}

// =============================================================================
// TaskMotores() — Consumidor da fila de comandos
//
// Roda no Core 1. Fica bloqueada em xQueueReceive() com timeout infinito,
// consumindo praticamente zero de CPU enquanto nenhum comando chega.
// Quando recebe um comando, aciona imediatamente os pinos da ponte H.
// =============================================================================
void TaskMotores(void* parametros) {
    Direcao cmd;

    Serial.println("[MOTORES] TaskMotores iniciada no Core 1.");

    for (;;) {
        // Bloqueia indefinidamente até um item entrar na fila.
        // portMAX_DELAY = suspende a task sem consumir CPU.
        if (xQueueReceive(filaComandos, &cmd, portMAX_DELAY) == pdTRUE) {
            processarComandoMotor(cmd);
        }
    }

    // Nunca alcança este ponto, mas é boa prática em FreeRTOS
    vTaskDelete(NULL);
}
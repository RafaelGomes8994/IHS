// =============================================================================
// camera_server.h — Driver de Câmera e Servidor HTTP MJPEG
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
//
// Responsabilidades deste módulo:
//   1. Inicializar o sensor OV2640 do ESP32-CAM (AI-Thinker)
//   2. Servir a interface web FPV em HTTP (porta 80)
//   3. Transmitir o vídeo via streaming MJPEG em /stream
//   4. Receber comandos de direção em /cmd?dir=X e empurrar para filaComandos
//
// Endpoints HTTP:
//   GET /         → Retorna a página HTML (web_page.h)
//   GET /stream   → Stream MJPEG multipart (Content-Type: multipart/x-mixed-replace)
//   GET /cmd?dir= → Aceita 'F' (frente), 'T' (trás), 'P' (parar)
//
// Integração com a arquitetura:
//   O servidor HTTP é gerenciado pelo ESP-IDF (esp_http_server.h) e roda
//   assincronamente no Core 0, compartilhando o rádio Wi-Fi com o stack TCP.
//   Os handlers HTTP são callbacks chamados pelo scheduler interno do httpd,
//   não bloqueando a TaskCamera nem o loop() do Arduino.
//
// NOTA DE HARDWARE:
//   A câmera e o rádio BLE compartilham trilhas de alimentação no ESP32-CAM.
//   Picos de corrente durante streaming podem causar Brownout. Use sempre uma
//   fonte externa de 5V/2A com cabo USB de qualidade (não adaptador FTDI).
// =============================================================================

#ifndef CAMERA_SERVER_H
#define CAMERA_SERVER_H

#include <Arduino.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// =============================================================================
// Pinagem do módulo AI-Thinker ESP32-CAM
// NÃO altere estes valores — são fixos no hardware do módulo.
// Pinos disponíveis para o usuário (não usados pela câmera):
//   GPIO 12, 13 (usados pelos motores), GPIO 14, 15 (usados pelos motores)
// =============================================================================
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1   // Sem pino de reset externo no AI-Thinker
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26   // SDA do I2C da câmera
#define CAM_PIN_SIOC    27   // SCL do I2C da câmera
#define CAM_PIN_D7      35   // Y9  — dados
#define CAM_PIN_D6      34   // Y8
#define CAM_PIN_D5      39   // Y7
#define CAM_PIN_D4      36   // Y6
#define CAM_PIN_D3      21   // Y5
#define CAM_PIN_D2      19   // Y4
#define CAM_PIN_D1      18   // Y3
#define CAM_PIN_D0       5   // Y2
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

// =============================================================================
// Configuração do streaming
// Resolução recomendada para FPV via Wi-Fi com ESP32-CAM:
//   FRAMESIZE_QVGA (320×240) → ~15 FPS, baixa latência
//   FRAMESIZE_VGA  (640×480) → ~8 FPS, melhor imagem
//   FRAMESIZE_SVGA (800×600) → ~5 FPS (pode travar com BLE ativo)
// =============================================================================
#define CAM_RESOLUCAO   FRAMESIZE_QVGA
#define CAM_QUALIDADE   12      // 0 (melhor) a 63 (pior) — 12 é bom equilíbrio

// =============================================================================
// Protótipos públicos
// =============================================================================

// Inicializa o sensor OV2640. Retorna true se bem-sucedido.
// Chame no setup() APÓS inicializarWiFi() (o clock I2C depende do Wi-Fi estar
// estabilizado para evitar interferência no barramento).
bool inicializarCamera();

// Inicia o servidor HTTP na porta 80 e registra os handlers de rota.
// Chame após inicializarCamera(). Se a câmera falhou, o servidor ainda sobe
// servindo a página de controle (sem o stream de vídeo).
void iniciarServidorCamera();

// Task FreeRTOS reservada para uso futuro (ex: processamento de imagem,
// detecção de obstáculos por visão computacional).
// Por enquanto, a câmera é totalmente event-driven pelos handlers do httpd.
void TaskCamera(void* parametros);

#endif // CAMERA_SERVER_H
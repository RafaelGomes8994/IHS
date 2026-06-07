// =============================================================================
// camera_server.cpp — Implementação do Servidor de Câmera MJPEG
// Projeto: Rover FPV | Disciplina: IHS — Prof. Bruno Prado / UFS
// =============================================================================

#include "camera_server.h"
#include "driver_motores.h"    // filaComandos, Direcao
#include "web_page.h"          // PAGINA_WEB (HTML da interface FPV)

// =============================================================================
// Handle do servidor HTTP (necessário para parar o servidor se precisar)
// =============================================================================
static httpd_handle_t servidorHTTP = NULL;

// Flag que indica se a câmera foi inicializada com sucesso
static bool cameraOK = false;

// =============================================================================
// Boundary do MJPEG multipart (padrão para streaming MJPEG em browsers)
// =============================================================================
#define PART_BOUNDARY "frame"
static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// =============================================================================
// inicializarCamera()
// Configura o sensor OV2640 com a pinagem do módulo AI-Thinker.
// =============================================================================
bool inicializarCamera() {
    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_sscb_sda = CAM_PIN_SIOD;
    config.pin_sscb_scl = CAM_PIN_SIOC;
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;        // 20 MHz — padrão OV2640
    config.pixel_format = PIXFORMAT_JPEG;  // JPEG direto do sensor (sem conversão na CPU)

    // Configura a resolução e buffer conforme a PSRAM disponível
    if (psramFound()) {
        // Com PSRAM: usa frames maiores e double-buffer para streaming fluido
        config.frame_size    = CAM_RESOLUCAO;
        config.jpeg_quality  = CAM_QUALIDADE;
        config.fb_count      = 2;   // Double-buffer: captura frame N+1 enquanto envia N
        Serial.println("[CAMERA] PSRAM detectada — modo double-buffer ativado.");
    } else {
        // Sem PSRAM: reduz resolução para caber no DRAM interno (320KB úteis)
        config.frame_size    = FRAMESIZE_QQVGA;  // 160×120 — cabe sem PSRAM
        config.jpeg_quality  = 20;
        config.fb_count      = 1;
        Serial.println("[CAMERA] AVISO: PSRAM não detectada — resolução reduzida para 160x120.");
    }

    esp_err_t resultado = esp_camera_init(&config);
    if (resultado != ESP_OK) {
        Serial.printf("[CAMERA] ERRO: Falha ao inicializar câmera (código 0x%x).\n", resultado);
        Serial.println("[CAMERA] Possíveis causas: alimentação insuficiente (Brownout), cabo FTDI.");
        Serial.println("[CAMERA] O servidor web subirá sem streaming de vídeo.");
        cameraOK = false;
        return false;
    }

    // Ajustes finos do sensor OV2640 para ambientes internos
    sensor_t* sensor = esp_camera_sensor_get();
    sensor->set_brightness(sensor, 1);     // +1 brilho (ambiente fechado)
    sensor->set_saturation(sensor, -1);    // -1 saturação (reduz artefatos JPEG)
    sensor->set_vflip(sensor, 1);          // Inverte vertical (montagem de cabeça para baixo)
    sensor->set_hmirror(sensor, 1);        // Espelha horizontal (câmera voltada para trás)

    Serial.println("[CAMERA] Câmera OV2640 inicializada com sucesso!");
    cameraOK = true;
    return true;
}

// =============================================================================
// Handler HTTP — GET /
// Serve a página HTML de controle FPV (definida em web_page.h)
// =============================================================================
static esp_err_t handlerPaginaWeb(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, PAGINA_WEB, HTTPD_RESP_USE_STRLEN);
}

// =============================================================================
// Handler HTTP — GET /stream
// Transmite frames MJPEG em loop (bloqueante — usa uma conexão HTTP longa).
// O browser renderiza como vídeo contínuo sem plugins.
// =============================================================================
static esp_err_t handlerStream(httpd_req_t* req) {
    if (!cameraOK) {
        // Câmera não disponível — retorna imagem de erro
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req,
            "Camera nao disponivel. Verifique a alimentacao.", HTTPD_RESP_USE_STRLEN);
    }

    esp_err_t res = ESP_OK;
    camera_fb_t* fb = NULL;
    char partBuf[128];

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        // Captura um frame do sensor
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("[CAMERA] AVISO: Falha ao capturar frame.");
            res = ESP_FAIL;
            break;
        }

        // Envia o boundary do multipart
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res != ESP_OK) break;

        // Envia o header da parte com o tamanho do JPEG
        size_t hdrLen = snprintf(partBuf, sizeof(partBuf),
                                 STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, partBuf, hdrLen);
        if (res != ESP_OK) break;

        // Envia o JPEG em si
        res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);

        // Devolve o frame buffer ao sensor IMEDIATAMENTE após envio
        // (crítico: manter o buffer ocupado bloqueia novos frames)
        esp_camera_fb_return(fb);
        fb = NULL;

        if (res != ESP_OK) break;

        // Cede CPU brevemente para o FreeRTOS processar outras tasks
        // (sem isso, o Core 0 ficaria 100% ocupado com o streaming)
        taskYIELD();
    }

    // Cleanup em caso de erro ou desconexão do cliente
    if (fb) esp_camera_fb_return(fb);
    return res;
}

// =============================================================================
// Handler HTTP — GET /cmd?dir=X
// Recebe comandos de direção via HTTP e os coloca na filaComandos.
// Parâmetro 'dir': 'F' = frente, 'T' = trás, 'P' = parar
// =============================================================================
static esp_err_t handlerComando(httpd_req_t* req) {
    char query[32];
    char param[4];
    Direcao dir = PARAR;

    // Extrai o query string da URL (ex: "dir=F")
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "dir", param, sizeof(param)) == ESP_OK) {
            switch (param[0]) {
                case 'F': case 'f': dir = FRENTE; break;
                case 'T': case 't': dir = TRAS;   break;
                default:            dir = PARAR;  break;
            }
        }
    }

    // Envia para a fila (não bloqueia — descarta se cheia)
    if (filaComandos != nullptr) {
        xQueueSend(filaComandos, &dir, 0);
    }

    // Responde com JSON para facilitar tratamento no JavaScript
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

// =============================================================================
// iniciarServidorCamera()
// Inicia o servidor HTTP e registra todas as rotas.
// =============================================================================
void iniciarServidorCamera() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port   = 80;
    config.max_uri_handlers = 8;
    // Aumenta o stack da task interna do httpd para suportar o streaming
    config.stack_size = 8192;

    if (httpd_start(&servidorHTTP, &config) != ESP_OK) {
        Serial.println("[CAMERA] ERRO: Falha ao iniciar servidor HTTP.");
        return;
    }

    // --- Rota: página web principal ---
    httpd_uri_t rotaPagina = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = handlerPaginaWeb,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(servidorHTTP, &rotaPagina);

    // --- Rota: streaming MJPEG ---
    httpd_uri_t rotaStream = {
        .uri      = "/stream",
        .method   = HTTP_GET,
        .handler  = handlerStream,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(servidorHTTP, &rotaStream);

    // --- Rota: comandos de direção ---
    httpd_uri_t rotaComando = {
        .uri      = "/cmd",
        .method   = HTTP_GET,
        .handler  = handlerComando,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(servidorHTTP, &rotaComando);

    Serial.println("[CAMERA] Servidor HTTP iniciado na porta 80.");
    Serial.println("[CAMERA] Rotas: /  |  /stream  |  /cmd?dir=");
}

// =============================================================================
// TaskCamera() — reservada para uso futuro
// =============================================================================
void TaskCamera(void* parametros) {
    // Placeholder: futuramente pode implementar análise de frames,
    // detecção de obstáculos ou compressão adaptativa de qualidade.
    Serial.println("[CAMERA] TaskCamera iniciada (modo passivo).");
    vTaskDelete(NULL); // Remove a si mesma — sem funcionalidade ainda
}
#include "driver_tcp.h"
#include "driver_motores.h"

WiFiServer server(8080);
extern QueueHandle_t filaComandos; // Referência à fila que está no main.cpp

void inicializarWiFi(const char* ssid, const char* password) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[TCP] Wi-Fi conectado! IP: ");
    Serial.println(WiFi.localIP());
}

void iniciarServidorTCP() {
    server.begin();
    Serial.println("[TCP] Servidor TCP iniciado na porta 8080.");
}

void verificarConexoesTCP() {
    WiFiClient client = server.available();
    if (client) {
        if (client.available()) {
            char comando = client.read();
            Direcao dir = PARAR;

            // Traduz o caractere recebido da rede para uma Direção
            switch (comando) {
                case 'F': dir = FRENTE; break;
                case 'T': dir = TRAS; break;
                default:  dir = PARAR; break;
            }
            
            // Envia para a fila (a mesma dos motores)
            xQueueSend(filaComandos, &dir, 0);
            client.println("Comando recebido");
        }
        client.stop();
    }
}
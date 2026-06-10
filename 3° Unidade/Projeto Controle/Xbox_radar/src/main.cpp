#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

int scanTime = 10; // Vai procurar durante 10 segundos
BLEScan* pBLEScan;

// O que acontece quando a ESP32 "ouve" algo no ar:
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveName()) {
            Serial.print("Dispositivo Encontrado: ");
            Serial.print(advertisedDevice.getName().c_str());
            Serial.print(" | MAC: ");
            Serial.println(advertisedDevice.getAddress().toString().c_str());
        }
    }
};

void setup() {
  Serial.begin(115200);
  
  // Aqui é o momento crítico onde a energia costumava cair:
  Serial.println("A ligar a Antena Bluetooth (Teste de Energia)...");
  BLEDevice::init("");
  
  // Se passar da linha acima sem reiniciar, o cabo funcionou!
  Serial.println("Antena ligada com sucesso! O cabo aguentou o pico de energia.");

  pBLEScan = BLEDevice::getScan(); 
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); 
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); 
}

void loop() {
  Serial.println("\n--- A procurar dispositivos Bluetooth (10s) ---");
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  
  Serial.print("Varredura concluída. Total de dispositivos: ");
  Serial.println(foundDevices.getCount());
  
  pBLEScan->clearResults();   
  delay(2000);
}
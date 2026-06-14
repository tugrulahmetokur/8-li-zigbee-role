// =============================================================================
//  config.h  —  Donanım ve davranış yapılandırması (TEK MERKEZ)
// -----------------------------------------------------------------------------
//  Tüm pin atamaları, röle tetikleme mantığı, zamanlamalar ve Zigbee
//  endpoint numaraları burada toplanmıştır. Donanımı değiştirdiğinizde
//  yalnızca bu dosyayı düzenlemeniz yeterlidir.
//
//  Hedef kart : ESP32-H2 (802.15.4 / Zigbee 3.0)
//  Ortam      : Arduino IDE + Arduino-ESP32 3.x
// =============================================================================
#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  1) RÖLE PIN HARİTASI
// -----------------------------------------------------------------------------
//  Röle indeksleri 0..7 (Zigbee'de endpoint 1..8 olarak görünür).
//  Kullanıcının verdiği bağlantı şeması:
//    R1->IO1  R2->IO2  R3->IO3  R4->IO4  R5->IO5  R6->IO10  R7->IO11  R8->IO12
// -----------------------------------------------------------------------------
#define RELAY_COUNT 8

static const uint8_t RELAY_PINS[RELAY_COUNT] = {
    1,   // Röle 1 -> GPIO1
    2,   // Röle 2 -> GPIO2
    3,   // Röle 3 -> GPIO3
    4,   // Röle 4 -> GPIO4
    5,   // Röle 5 -> GPIO5
    10,  // Röle 6 -> GPIO10
    11,  // Röle 7 -> GPIO11
    12   // Röle 8 -> GPIO12
};

// -----------------------------------------------------------------------------
//  2) RÖLE TETİKLEME SEVİYESİ (Active-HIGH / Active-LOW)
// -----------------------------------------------------------------------------
//  Optokuplörlü/izolasyonlu kartların çoğu "Active-LOW"dur: röleyi çekmek
//  (ON yapmak) için pini LOW'a çekmek gerekir. Doğrudan transistörlü
//  sürücüler ise genelde "Active-HIGH"dır.
//
//  Kartınıza göre AŞAĞIDAKİ TEK SATIRI değiştirin:
//    - Active-HIGH kart : RELAY_ACTIVE_LEVEL = HIGH
//    - Active-LOW  kart : RELAY_ACTIVE_LEVEL = LOW
// -----------------------------------------------------------------------------
#define RELAY_ACTIVE_LEVEL LOW   // <-- izolasyonlu kart varsayılanı (Active-LOW)

// "ON" ve "OFF" durumlarında pine yazılacak fiziksel seviyeler:
#define RELAY_LEVEL_ON  (RELAY_ACTIVE_LEVEL)
#define RELAY_LEVEL_OFF (!RELAY_ACTIVE_LEVEL)

// -----------------------------------------------------------------------------
//  3) SICAKLIK SENSÖRÜ (DS18B20 / OneWire)
// -----------------------------------------------------------------------------
#define ONEWIRE_PIN          13      // DS18B20 veri hattı -> GPIO13
#define DS18B20_RESOLUTION    12     // 9..12 bit (12 bit = 0.0625 C, ~750ms)

// Asenkron okuma zamanlaması (millis tabanlı; delay() KULLANILMAZ)
#define TEMP_READ_INTERVAL_MS  30000UL  // 30 sn'de bir yeni ölçüm tetikle
#define TEMP_CONVERSION_MS       800UL  // 12-bit dönüşüm için bekleme penceresi

// Zigbee Temperature Measurement cluster sınır/raporlama ayarları
#define TEMP_MIN_C            -40.0f
#define TEMP_MAX_C            125.0f
#define TEMP_TOLERANCE_C        0.5f

// -----------------------------------------------------------------------------
//  4) ZIGBEE ENDPOINT NUMARALARI
// -----------------------------------------------------------------------------
//  Her cluster için ayrı bir endpoint kullanıyoruz.
//  Röleler   : endpoint 1..8  (On/Off Cluster 0x0006)
//  Sıcaklık  : endpoint 9     (Temperature Measurement Cluster 0x0402)
// -----------------------------------------------------------------------------
#define ZB_RELAY_ENDPOINT_BASE  1            // röle 0 -> endpoint 1
#define ZB_TEMP_ENDPOINT       (ZB_RELAY_ENDPOINT_BASE + RELAY_COUNT)  // = 9

#define ZB_MANUFACTURER "DIY-Industrial"
#define ZB_RELAY_MODEL  "ZB-8CH-Relay"
#define ZB_TEMP_MODEL   "ZB-DS18B20"

// -----------------------------------------------------------------------------
//  5) WATCHDOG (WDT) ve diğer zamanlamalar
// -----------------------------------------------------------------------------
#define WDT_TIMEOUT_MS        10000   // 10 sn içinde reset gelmezse panik+reboot
#define LOOP_HEARTBEAT_MS      1000   // periyodik durum logu

// BOOT butonu ile fabrika ayarı / yeniden eşleşme (opsiyonel)
#define FACTORY_RESET_PIN         9   // ESP32-H2 BOOT butonu = GPIO9
#define FACTORY_RESET_HOLD_MS  5000   // 5 sn basılı tut -> ağdan çık + temizle

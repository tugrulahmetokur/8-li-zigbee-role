// =============================================================================
//  8-li-zigbee-role.ino
//  ESP32-H2 Zigbee Router: 8 Kanal Röle + DS18B20 Sıcaklık Düğümü
// -----------------------------------------------------------------------------
//  Mimari özet:
//    * Zigbee ROUTER olarak ağa katılır (sürekli enerjili cihaz -> menzili
//      genişletir, mesh repeater görevi görür).
//    * 8 adet On/Off endpoint (Cluster 0x0006) -> her biri bir röleyi sürer.
//    * 1 adet Temperature Measurement endpoint (Cluster 0x0402) -> DS18B20.
//    * Röle durumları NVS'te tutulur, boot'ta otomatik geri yüklenir.
//    * DS18B20 okuması ASENKRON yapılır (millis state-machine; delay YOK).
//    * Task Watchdog (WDT) ana döngüyü korur.
//
//  ----------------------------------------------------------------------------
//  ARDUINO IDE KURULUMU (önemli!):
//    1) Boards Manager -> "esp32" paketi 3.x kurun (Espressif Systems).
//    2) Tools > Board            -> "ESP32-H2 Dev Module"
//    3) Tools > Zigbee mode      -> "Zigbee ZCZR (coordinator/router)"
//    4) Tools > Partition Scheme -> "Zigbee ZCZR" (zb_storage + zb_fct şart)
//    5) Library Manager'dan kurun:
//         - "OneWire"           (Paul Stoffregen)
//         - "DallasTemperature" (Miles Burton)
//       (Zigbee, Preferences, esp_task_wdt çekirdekte gelir; kurulum gerekmez.)
// =============================================================================

#include <Arduino.h>
#include "esp_task_wdt.h"     // Task Watchdog
#include <OneWire.h>          // DS18B20 1-Wire
#include <DallasTemperature.h>

#include "Zigbee.h"           // Arduino-ESP32 Zigbee kütüphanesi (3.x)
#include "config.h"
#include "RelayController.h"

// =============================================================================
//  GLOBAL NESNELER
// =============================================================================
RelayController relays;

// Zigbee endpoint nesneleri (röleler endpoint 1..8)
ZigbeeLight zbRelays[RELAY_COUNT] = {
    ZigbeeLight(ZB_RELAY_ENDPOINT_BASE + 0), ZigbeeLight(ZB_RELAY_ENDPOINT_BASE + 1),
    ZigbeeLight(ZB_RELAY_ENDPOINT_BASE + 2), ZigbeeLight(ZB_RELAY_ENDPOINT_BASE + 3),
    ZigbeeLight(ZB_RELAY_ENDPOINT_BASE + 4), ZigbeeLight(ZB_RELAY_ENDPOINT_BASE + 5),
    ZigbeeLight(ZB_RELAY_ENDPOINT_BASE + 6), ZigbeeLight(ZB_RELAY_ENDPOINT_BASE + 7),
};
ZigbeeTempSensor zbTemp(ZB_TEMP_ENDPOINT);   // sıcaklık endpoint 9

// DS18B20
OneWire           oneWire(ONEWIRE_PIN);
DallasTemperature ds18b20(&oneWire);

// =============================================================================
//  ZIGBEE -> RÖLE GERİ ÇAĞRIMLARI (callback)
// -----------------------------------------------------------------------------
//  Arduino Zigbee API'si onLightChange(void(*)(bool)) imzasını kullanır ve
//  endpoint indeksini geçirmez. Bu yüzden her röle için derleme zamanında
//  benzersiz bir "trampoline" fonksiyonu üretiyoruz (template + indeks).
//  Böylece 8 ayrı fonksiyonu elle yazmadan, temiz şekilde bağlıyoruz.
// =============================================================================
template <uint8_t IDX>
void onZbRelayChange(bool state) {
    // Zigbee koordinatöründen gelen komut -> röleyi sür ve NVS'e yaz.
    relays.set(IDX, state, /*persist=*/true);
}

// Zigbee tarafının görüntülediği durumu, gerçek (NVS'ten gelen) durumla eşitler.
void syncZbStateFromRelays() {
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        zbRelays[i].setLight(relays.get(i));   // raporlanan attribute'u güncelle
    }
}

// =============================================================================
//  WATCHDOG
// =============================================================================
void setupWatchdog() {
    // Arduino çekirdeği loopTask için TWDT'yi bazen önceden başlatır; bu yüzden
    // init yerine reconfigure deneyip idempotent davranıyoruz.
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0),   // H2 tek çekirdek (core0)
        .trigger_panic  = true        // süre dolarsa panik -> otomatik reboot
    };
    if (esp_task_wdt_reconfigure(&wdt_cfg) != ESP_OK) {
        esp_task_wdt_init(&wdt_cfg);  // henüz başlatılmadıysa
    }
    esp_task_wdt_add(NULL);           // mevcut task'ı (loop) izlemeye al
    Serial.printf("[WDT] Aktif (timeout=%lu ms)\n", (unsigned long)WDT_TIMEOUT_MS);
}

// =============================================================================
//  DS18B20 — ASENKRON OKUMA STATE MACHINE
// -----------------------------------------------------------------------------
//  setWaitForConversion(false) ile requestTemperatures() bloklamaz; dönüşüm
//  tamamlanana kadar (~750ms) bekleyip sonucu okuruz. Bu süreçte Zigbee
//  stack'i ve ana döngü çalışmaya devam eder (delay YOK).
// =============================================================================
enum class TempState { IDLE, CONVERTING };
TempState  tempState     = TempState::IDLE;
uint32_t   tempLastCycle = 0;   // son ölçüm tetikleme anı
uint32_t   tempReqAt     = 0;   // dönüşüm isteği gönderim anı

void setupTempSensor() {
    ds18b20.begin();
    ds18b20.setResolution(DS18B20_RESOLUTION);
    ds18b20.setWaitForConversion(false);   // <-- ASENKRON modun anahtarı

    if (ds18b20.getDeviceCount() == 0)
        Serial.println("[TEMP] UYARI: DS18B20 bulunamadi (IO13 + 4.7k pull-up kontrol edin)");
    else
        Serial.println("[TEMP] DS18B20 hazir");
}

void serviceTempSensor() {
    const uint32_t now = millis();

    switch (tempState) {
        case TempState::IDLE:
            // 30 sn'de bir yeni dönüşüm başlat (non-blocking).
            if (now - tempLastCycle >= TEMP_READ_INTERVAL_MS) {
                ds18b20.requestTemperatures();   // bloklamaz
                tempReqAt     = now;
                tempLastCycle = now;
                tempState     = TempState::CONVERTING;
            }
            break;

        case TempState::CONVERTING:
            // Dönüşüm penceresi dolunca sonucu oku ve Zigbee'ye raporla.
            if (now - tempReqAt >= TEMP_CONVERSION_MS) {
                float c = ds18b20.getTempCByIndex(0);
                if (c != DEVICE_DISCONNECTED_C && c > -100.0f) {
                    zbTemp.setTemperature(c);    // 0x0402 attribute güncelle + raporla
                    Serial.printf("[TEMP] %.2f C -> Zigbee'ye raporlandi\n", c);
                } else {
                    Serial.println("[TEMP] Okuma hatasi (sensor baglantisi?)");
                }
                tempState = TempState::IDLE;
            }
            break;
    }
}

// =============================================================================
//  FABRİKA AYARI — BOOT butonu uzun basış (opsiyonel, non-blocking)
// =============================================================================
void serviceFactoryReset() {
    static uint32_t pressedAt = 0;
    // BOOT butonu basılıyken pin LOW (dahili pull-up).
    bool pressed = (digitalRead(FACTORY_RESET_PIN) == LOW);

    if (pressed) {
        if (pressedAt == 0) pressedAt = millis();
        else if (millis() - pressedAt >= FACTORY_RESET_HOLD_MS) {
            Serial.println("[SYS] Fabrika ayarina donuluyor: roleler temizleniyor + Zigbee leave");
            relays.clear();
            Zigbee.factoryReset();   // ağdan çık + Zigbee NVRAM temizle + reboot
        }
    } else {
        pressedAt = 0;
    }
}

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(50);   // yalnızca seri portun oturması için (Zigbee başlamadan önce)
    Serial.println("\n[SYS] ESP32-H2 8CH Zigbee Role + DS18B20 baslatiliyor...");

    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);

    // 1) GÜVENLİ BAŞLATMA: pinleri OFF'a çek, ardından NVS'ten son durumu yükle.
    relays.begin();

    // 2) DS18B20 asenkron okuma kurulumu.
    setupTempSensor();

    // 3) Zigbee endpoint'lerini yapılandır.
    //    --- 8x On/Off röle endpoint'i ---
    zbRelays[0].onLightChange(onZbRelayChange<0>);
    zbRelays[1].onLightChange(onZbRelayChange<1>);
    zbRelays[2].onLightChange(onZbRelayChange<2>);
    zbRelays[3].onLightChange(onZbRelayChange<3>);
    zbRelays[4].onLightChange(onZbRelayChange<4>);
    zbRelays[5].onLightChange(onZbRelayChange<5>);
    zbRelays[6].onLightChange(onZbRelayChange<6>);
    zbRelays[7].onLightChange(onZbRelayChange<7>);
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        zbRelays[i].setManufacturerAndModel(ZB_MANUFACTURER, ZB_RELAY_MODEL);
        Zigbee.addEndpoint(&zbRelays[i]);
    }

    //    --- 1x Sıcaklık endpoint'i (0x0402) ---
    zbTemp.setManufacturerAndModel(ZB_MANUFACTURER, ZB_TEMP_MODEL);
    zbTemp.setMinMaxValue(TEMP_MIN_C, TEMP_MAX_C);
    zbTemp.setTolerance(TEMP_TOLERANCE_C);
    // Raporlama: min 0s, max 30s, delta 0.5C -> koordinatöre periyodik bildirim.
    zbTemp.setReporting(0, 30, TEMP_TOLERANCE_C);
    Zigbee.addEndpoint(&zbTemp);

    // 4) ROUTER modunda ağa katıl.
    Serial.println("[ZB] Router olarak baslatiliyor...");
    if (!Zigbee.begin(ZIGBEE_ROUTER)) {
        Serial.println("[ZB] HATA: Zigbee baslatilamadi -> yeniden baslatiliyor");
        delay(1000);
        ESP.restart();
    }

    // Ağ bağlantısını bekle. (Henüz WDT eklenmedi; setup'ta beklemek güvenli.)
    Serial.print("[ZB] Aga baglaniliyor");
    while (!Zigbee.connected()) {
        Serial.print(".");
        delay(100);
    }
    Serial.println(" baglandi!");

    // 5) Zigbee'nin gördüğü durumu, NVS'ten yüklenen gerçek durumla eşitle.
    syncZbStateFromRelays();

    // 6) Watchdog'u en son devreye al (uzun init/ağ bekleme bitince).
    setupWatchdog();

    Serial.println("[SYS] Hazir.");
}

// =============================================================================
//  LOOP  (bloklamayan; her geçişte WDT beslenir)
// =============================================================================
void loop() {
    esp_task_wdt_reset();      // <-- Watchdog'u besle (takılma olursa reset gelir)

    serviceTempSensor();       // asenkron DS18B20
    serviceFactoryReset();     // BOOT uzun basış

    // Periyodik kalp atışı / bağlantı durumu logu.
    static uint32_t hb = 0;
    if (millis() - hb >= LOOP_HEARTBEAT_MS) {
        hb = millis();
        if (!Zigbee.connected())
            Serial.println("[ZB] Ag baglantisi yok - yeniden katilim bekleniyor...");
    }

    // Stack'e nefes aldır (busy-wait yapma).
    delay(5);
}

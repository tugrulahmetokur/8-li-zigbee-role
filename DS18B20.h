// =============================================================================
//  DS18B20.h  —  Bağımlılıksız (kütüphanesiz) DS18B20 / 1-Wire sürücüsü
// -----------------------------------------------------------------------------
//  NEDEN BU DOSYA VAR?
//    Klasik "OneWire" (Paul Stoffregen) kütüphanesi GPIO'lara doğrudan register
//    erişimi yapar ve ESP32-H2'nin yeni gpio_struct yapısı (esp32 core 3.3.x)
//    ile DERLENMEZ. Bu mini sürücü, tek bir DS18B20 için gereken 1-Wire
//    protokolünü ESP-IDF'in TAŞINABİLİR GPIO API'si (driver/gpio.h) ile
//    uygular; böylece harici OneWire/DallasTemperature kütüphanelerine HİÇ
//    ihtiyaç kalmaz ve kütüphane çakışması imkânsız hale gelir.
//
//  ELEKTRİKSEL NOT:
//    DS18B20 VERİ hattı (IO13) ile 3V3 arasına 4.7kΩ pull-up ŞARTTIR.
//    Sensör harici besleme (VDD=3V3) ile çalıştırılmalı (parazit güç DEĞİL).
//
//  ZAMANLAMA NOTU:
//    Pin "open-drain + dahili/harici pull-up" olarak yapılandırılır; LOW'a
//    çekmek = sür, HIGH = bırak (pull-up kaldırır). Bit-seviyesi kritik
//    pencereler noInterrupts() ile korunur (her biri < ~70µs).
// =============================================================================
#pragma once

#include <Arduino.h>
#include "driver/gpio.h"

class DS18B20 {
public:
    explicit DS18B20(uint8_t pin) : _gpio((gpio_num_t)pin) {}

    // Pini open-drain giriş/çıkış + pull-up olarak yapılandır (boşta HIGH).
    void begin() {
        gpio_set_direction(_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode(_gpio, GPIO_PULLUP_ONLY);  // harici 4.7k yine de gerekli
        gpio_set_level(_gpio, 1);                     // hattı bırak (HIGH)
    }

    // Sensörün hatta olup olmadığını presence-pulse ile kontrol et.
    bool isPresent() { return reset(); }

    // 0x44 "Convert T" komutunu gönderir (SKIP ROM). Dönüşüm sensörde başlar;
    // bu fonksiyon BEKLEMEZ (asenkron). Dönüş: sensör mevcutsa true.
    bool startConversion() {
        if (!reset()) return false;
        writeByte(0xCC);   // SKIP ROM (tek sensör)
        writeByte(0x44);   // CONVERT T
        return true;
    }

    // Scratchpad'i okur, CRC doğrular ve °C döndürür. Başarılıysa true.
    // (Dönüşümün tamamlanması için startConversion()'dan sonra >=750ms geçmeli.)
    bool readTemperature(float &outC) {
        if (!reset()) return false;
        writeByte(0xCC);   // SKIP ROM
        writeByte(0xBE);   // READ SCRATCHPAD

        uint8_t data[9];
        for (uint8_t i = 0; i < 9; i++) data[i] = readByte();

        if (crc8(data, 8) != data[8]) return false;          // CRC hatası -> at
        if (data[0] == 0x50 && data[1] == 0x05) return false; // 85°C güç-on değeri

        int16_t raw = (int16_t)((data[1] << 8) | data[0]);
        outC = raw * 0.0625f;   // 12-bit çözünürlük (fabrika varsayılanı)
        return true;
    }

private:
    gpio_num_t _gpio;

    inline void driveLow() { gpio_set_level(_gpio, 0); }  // hattı LOW'a çek
    inline void release()  { gpio_set_level(_gpio, 1); }  // bırak (pull-up HIGH)
    inline int  readPin()  { return gpio_get_level(_gpio); }

    // Reset darbesi + presence algılama. Sensör varsa true.
    bool reset() {
        driveLow();
        delayMicroseconds(480);
        release();
        delayMicroseconds(70);
        bool present = (readPin() == 0);   // sensör hattı LOW'a çeker = mevcut
        delayMicroseconds(410);
        return present;
    }

    void writeBit(bool bit) {
        noInterrupts();
        driveLow();
        delayMicroseconds(bit ? 6 : 60);   // '1' kısa, '0' uzun LOW
        release();
        delayMicroseconds(bit ? 64 : 10);
        interrupts();
    }

    uint8_t readBit() {
        noInterrupts();
        driveLow();
        delayMicroseconds(3);
        release();
        delayMicroseconds(10);             // örnekleme penceresi (<15µs)
        uint8_t b = readPin() ? 1 : 0;
        interrupts();
        delayMicroseconds(50);             // slot toparlanması
        return b;
    }

    void writeByte(uint8_t v) {
        for (uint8_t i = 0; i < 8; i++) { writeBit(v & 0x01); v >>= 1; }  // LSB-first
    }

    uint8_t readByte() {
        uint8_t v = 0;
        for (uint8_t i = 0; i < 8; i++) if (readBit()) v |= (1 << i);
        return v;
    }

    // Dallas/Maxim 1-Wire CRC8 (polinom 0x31 / yansıtılmış 0x8C).
    static uint8_t crc8(const uint8_t *d, uint8_t len) {
        uint8_t crc = 0;
        while (len--) {
            uint8_t in = *d++;
            for (uint8_t i = 0; i < 8; i++) {
                uint8_t mix = (crc ^ in) & 0x01;
                crc >>= 1;
                if (mix) crc ^= 0x8C;
                in >>= 1;
            }
        }
        return crc;
    }
};

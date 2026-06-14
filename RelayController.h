// =============================================================================
//  RelayController.h  —  8 kanallı röle yönetimi + NVS durum hafızası
// -----------------------------------------------------------------------------
//  Sorumluluklar:
//    * Güvenli başlatma (Safe Init): pinleri OUTPUT yap, floating önle,
//      önce HEPSİNİ güvenli (OFF) konuma çek.
//    * NVS (Preferences) üzerinden son röle durumlarını sakla/geri yükle.
//    * Active-HIGH / Active-LOW farkını soyutla (bkz. config.h).
//
//  Durumlar 8 bitlik tek bir maske (uint8_t) olarak NVS'e yazılır; bu sayede
//  flash'a yazma minimumda tutulur (tek anahtar, tek byte = düşük aşınma).
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

class RelayController {
public:
    // Güvenli başlatma + NVS'ten geri yükleme.
    void begin() {
        // 1) Pinleri ÇIKIŞ yap ve DERHAL güvenli (OFF) seviyeye çek.
        //    Bu adım, enerji geldiği an pinlerin floating kalmasını ve
        //    rölelerin rastgele tetiklenmesini engeller.
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            pinMode(RELAY_PINS[i], OUTPUT);
            digitalWrite(RELAY_PINS[i], RELAY_LEVEL_OFF);
        }

        // 2) NVS'i aç ve son kaydedilen durum maskesini oku (yoksa 0 = hepsi OFF).
        _prefs.begin(_NVS_NAMESPACE, /*readOnly=*/false);
        _states = _prefs.getUChar(_NVS_KEY, 0x00);

        // 3) Güvenli konumdan, kayıtlı son duruma geç.
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            _writePin(i, _bit(i));
        }

        Serial.printf("[RELAY] Geri yuklenen durum maskesi: 0x%02X\n", _states);
    }

    // Bir röleyi aç/kapat. persist=true ise değişikliği NVS'e yazar.
    // Dönüş: durum gerçekten değiştiyse true.
    bool set(uint8_t idx, bool on, bool persist = true) {
        if (idx >= RELAY_COUNT) return false;
        if (_bit(idx) == on) return false;          // değişiklik yok -> flash'ı yorma

        _setBit(idx, on);
        _writePin(idx, on);
        if (persist) _save();

        Serial.printf("[RELAY] R%u -> %s\n", idx + 1, on ? "ON" : "OFF");
        return true;
    }

    bool get(uint8_t idx) const {
        return (idx < RELAY_COUNT) ? _bit(idx) : false;
    }

    uint8_t mask() const { return _states; }

    // Fabrika ayarı / temizlik: tüm röleleri OFF yap ve NVS'i sıfırla.
    void clear() {
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            _setBit(i, false);
            _writePin(i, false);
        }
        _save();
    }

private:
    // --- yardımcılar -------------------------------------------------------
    bool _bit(uint8_t i) const { return (_states >> i) & 0x01; }
    void _setBit(uint8_t i, bool v) {
        if (v) _states |=  (1u << i);
        else   _states &= ~(1u << i);
    }

    // Mantıksal ON/OFF -> fiziksel seviye (active-level'a göre).
    void _writePin(uint8_t idx, bool on) {
        digitalWrite(RELAY_PINS[idx], on ? RELAY_LEVEL_ON : RELAY_LEVEL_OFF);
    }

    void _save() {
        _prefs.putUChar(_NVS_KEY, _states);   // tek byte; hızlı ve aşınmayı azaltır
    }

    Preferences _prefs;
    uint8_t     _states = 0;                  // bit i = röle i durumu (1=ON)

    static constexpr const char* _NVS_NAMESPACE = "relayst";
    static constexpr const char* _NVS_KEY       = "mask";
};

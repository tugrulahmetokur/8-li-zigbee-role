// =============================================================================
//  RelayController.h  —  8 kanallı röle yönetimi + NVS + KADEMELİ anahtarlama
// -----------------------------------------------------------------------------
//  Sorumluluklar:
//    * Güvenli başlatma (Safe Init): pinleri OUTPUT yap, floating önle,
//      önce HEPSİNİ güvenli (OFF) konuma çek.
//    * NVS (Preferences) üzerinden son röle "hedef" durumunu sakla/geri yükle.
//    * Active-HIGH / Active-LOW farkını soyutla (bkz. config.h).
//    * ANI YÜK KORUMASI: röleler eşzamanlı değil, aralarında en az
//      RELAY_SWITCH_GAP_MS olacak şekilde TEK TEK anahtarlanır. Bu işlem
//      delay() ile DEĞİL, millis tabanlı bir kuyrukla (service()) yapılır;
//      böylece Zigbee stack bloklanmaz.
//
//  İki ayrı durum tutulur:
//    _desired : istenen/hedef durum (Zigbee komutu veya NVS'ten gelir)
//    _applied : pinlere fiziksel olarak yazılmış güncel durum
//  service() her çağrıldığında, gap süresi dolduysa _applied'ı _desired'a
//  doğru BİR ADIM (tek röle) yaklaştırır.
//
//  NVS'e _desired anında yazılır: güç kesilse bile son komut hatırlanır;
//  reboot sonrası yine kademeli olarak uygulanır.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

class RelayController {
public:
    // Güvenli başlatma + NVS'ten hedef durumu geri yükleme.
    // Not: fiziksel uygulama BURADA yapılmaz; service() tarafından kademeli
    // olarak gerçekleştirilir (boot inrush'ını önlemek için).
    void begin() {
        // 1) Pinleri ÇIKIŞ yap ve DERHAL güvenli (OFF) seviyeye çek.
        //    Enerji geldiği an floating / rastgele tetikleme engellenir.
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            pinMode(RELAY_PINS[i], OUTPUT);
            digitalWrite(RELAY_PINS[i], RELAY_LEVEL_OFF);
        }
        _applied = 0x00;   // fiziksel: hepsi OFF

        // 2) NVS'ten son HEDEF durum maskesini oku (yoksa 0 = hepsi OFF).
        _prefs.begin(_NVS_NAMESPACE, /*readOnly=*/false);
        _desired = _prefs.getUChar(_NVS_KEY, 0x00);

        // 3) İlk anahtarlamanın hemen yapılabilmesi için zamanlayıcıyı geriye al.
        _lastSwitchMs = millis() - RELAY_SWITCH_GAP_MS;

        Serial.printf("[RELAY] Hedef durum 0x%02X -> kademeli uygulanacak "
                      "(her %lu ms'de 1 role)\n",
                      _desired, (unsigned long)RELAY_SWITCH_GAP_MS);
    }

    // Zigbee/üst katmandan komut: yalnızca HEDEFİ günceller ve NVS'e yazar.
    // Fiziksel anahtarlama service() tarafından kademeli yapılır.
    // Dönüş: hedef gerçekten değiştiyse true.
    bool set(uint8_t idx, bool on, bool persist = true) {
        if (idx >= RELAY_COUNT) return false;
        if (bitOf(_desired, idx) == on) return false;   // değişiklik yok

        setBit(_desired, idx, on);
        if (persist) _save();

        Serial.printf("[RELAY] R%u hedef -> %s (kuyruga alindi)\n",
                      idx + 1, on ? "ON" : "OFF");
        return true;
    }

    // loop() içinden sürekli çağrılır. Gap süresi dolduysa, _applied ile
    // _desired arasındaki İLK farklı röleyi anahtarlar (tek adım).
    // Bu sayede birden çok röle hiçbir zaman aynı anda çekilmez.
    void service() {
        if (_applied == _desired) return;                 // yapılacak iş yok
        if (millis() - _lastSwitchMs < RELAY_SWITCH_GAP_MS) return;  // gap dolmadı

        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            bool want = bitOf(_desired, i);
            if (bitOf(_applied, i) != want) {
                _writePin(i, want);
                setBit(_applied, i, want);
                _lastSwitchMs = millis();
                Serial.printf("[RELAY] R%u fiziksel -> %s\n", i + 1, want ? "ON" : "OFF");
                return;   // bu turda yalnızca BİR röle anahtarla
            }
        }
    }

    // Hedef durum (Zigbee attribute senkronizasyonu için).
    bool desired(uint8_t idx) const {
        return (idx < RELAY_COUNT) ? bitOf(_desired, idx) : false;
    }
    // Fiziksel/uygulanmış durum.
    bool applied(uint8_t idx) const {
        return (idx < RELAY_COUNT) ? bitOf(_applied, idx) : false;
    }
    bool settled() const { return _applied == _desired; }   // kuyruk boş mu?

    // Fabrika ayarı / acil durum: tüm röleleri DERHAL OFF yap (kapatmada
    // inrush riski olmadığı için beklemeye gerek yok) ve NVS'i sıfırla.
    void clear() {
        for (uint8_t i = 0; i < RELAY_COUNT; i++) _writePin(i, false);
        _desired = 0;
        _applied = 0;
        _save();
    }

private:
    // --- bit yardımcıları --------------------------------------------------
    static bool bitOf(uint8_t mask, uint8_t i) { return (mask >> i) & 0x01; }
    static void setBit(uint8_t &mask, uint8_t i, bool v) {
        if (v) mask |=  (1u << i);
        else   mask &= ~(1u << i);
    }

    // Mantıksal ON/OFF -> fiziksel seviye (active-level'a göre).
    void _writePin(uint8_t idx, bool on) {
        digitalWrite(RELAY_PINS[idx], on ? RELAY_LEVEL_ON : RELAY_LEVEL_OFF);
    }

    void _save() {
        _prefs.putUChar(_NVS_KEY, _desired);   // tek byte; düşük aşınma
    }

    Preferences _prefs;
    uint8_t     _desired      = 0;   // hedef durum maskesi
    uint8_t     _applied      = 0;   // fiziksel durum maskesi
    uint32_t    _lastSwitchMs = 0;   // son fiziksel anahtarlama anı

    static constexpr const char* _NVS_NAMESPACE = "relayst";
    static constexpr const char* _NVS_KEY       = "mask";
};

# ESP32-H2 · 8 Kanal Zigbee Röle + DS18B20 Sıcaklık Düğümü (Arduino IDE)

ESP32-H2 geliştirme kartını, Zigbee 3.0 ağına **Router** olarak katılan,
8 kanallı röle kontrolcüsü ve ortam sıcaklık sensörü düğümüne dönüştüren
**Arduino IDE** firmware iskeleti.

## Özellikler

| Gereksinim | Karşılanma şekli |
|---|---|
| Zigbee Router | `Zigbee.begin(ZIGBEE_ROUTER)` — sürekli enerjili, mesh repeater |
| 8x On/Off (Cluster `0x0006`) | 8 ayrı `ZigbeeLight` endpoint (1..8) |
| Sıcaklık (Cluster `0x0402`) | `ZigbeeTempSensor` endpoint (9), DS18B20 |
| Durum hafızası (NVS) | `Preferences` ile tek byte'lık durum maskesi; boot'ta geri yükleme |
| Asenkron sensör okuma | `millis()` state-machine + `setWaitForConversion(false)` — **bloklama yok** |
| Ani yük (inrush) koruması | Röleler eşzamanlı değil, aralarında ≥ `RELAY_SWITCH_GAP_MS` ile **tek tek** anahtarlanır (millis kuyruğu, bloklamasız) |
| Güvenli başlatma | Pinler önce OFF'a çekilir (floating yok), sonra NVS durumu uygulanır |
| Active-HIGH / Active-LOW | `config.h` içinde tek satır: `RELAY_ACTIVE_LEVEL` |
| Watchdog | `esp_task_wdt` ile ana döngü korunur |

## Donanım / Pin Haritası

| İşlev | GPIO |
|---|---|
| Röle 1..5 | IO1, IO2, IO3, IO4, IO5 |
| Röle 6..8 | IO10, IO11, IO12 |
| DS18B20 (OneWire) | IO13 (+ 4.7kΩ pull-up → 3V3) |
| BOOT (fabrika ayarı) | IO9 |

> **Not:** İzolasyonlu/optokuplörlü röle kartları genelde **Active-LOW**'dur.
> Varsayılan `RELAY_ACTIVE_LEVEL = LOW` olarak ayarlandı. Kartınız Active-HIGH
> ise `config.h` içinde tek satırı `HIGH` yapın.

## Dosya Yapısı (Arduino Sketch)

Arduino IDE'de sketch klasörü ile `.ino` dosyası **aynı isimde** olmalıdır.
Aynı klasördeki `.h` dosyaları otomatik derlenir.

```
8-li-zigbee-role/
├── 8-li-zigbee-role.ino   # Ana sketch: Zigbee kurulumu, asenkron sensör, WDT
├── config.h               # Pin/ayar merkezi (active-level, zamanlama, endpoint)
└── RelayController.h       # 8 röle sınıfı + NVS durum hafızası + güvenli init
```

## Neden Arduino-ESP32 (ESP-IDF yerine)?

ESP32-H2 Zigbee için iki yol vardır:

1. **ESP-IDF + esp-zigbee-sdk** — En düşük seviye, maksimum kontrol; ancak her
   cluster/attribute elle tanımlanır, öğrenme eğrisi diktir.
2. **Arduino-ESP32 3.x Zigbee kütüphanesi** — esp-zigbee-sdk'yı `ZigbeeLight`,
   `ZigbeeTempSensor` gibi hazır sınıflarla sarmalar.

Bu proje **(2)**'yi (Arduino IDE) tercih eder çünkü:
- İstenen her şey (`millis()`, `Preferences`/NVS, `OneWire`/`DallasTemperature`)
  Arduino ekosisteminde hazır ve olgundur.
- 8 On/Off + 1 Temperature endpoint birkaç satırda kurulur; IDF'te bu yüzlerce
  satır boilerplate demektir.
- WDT, NVS gibi "endüstriyel" gereksinimler Arduino katmanından da IDF
  API'leriyle (`esp_task_wdt.h`) doğrudan kullanılabilir — esneklikten ödün yok.

İleride mikrosaniye seviyesinde zamanlama veya özel cluster gerekirse ESP-IDF'e
geçiş mantıklıdır; bu iskelet o aşamaya kadar fazlasıyla yeterlidir.

## Kurulum — Arduino IDE

1. **Boards Manager** → `esp32` paketini **3.x** kurun (Espressif Systems).
2. **Tools → Board** → *ESP32-H2 Dev Module*.
3. **Tools → Zigbee mode** → *Zigbee ZCZR (coordinator/router)*.
   - Bu seçim, Router yığınını derler (`-DZIGBEE_MODE_ZCZR`).
4. **Tools → Partition Scheme** → *Zigbee ZCZR*.
   - Zigbee için `zb_storage` + `zb_fct` partition'larını içerir (**zorunlu**).
5. **Library Manager** → şu kütüphaneleri kurun:
   - **OneWireNg** (Piotr Stolarz) — ESP32-H2 uyumlu 1-Wire kütüphanesi.
   - **DallasTemperature** (Miles Burton)
   - *(Zigbee, Preferences, esp_task_wdt çekirdekte gelir — ayrı kurulum yok.)*
   - ⚠️ Klasik **OneWire** (Paul Stoffregen) kuruluysa **KALDIRIN** — ESP32-H2'de
     derlenmez (bkz. Sorun Giderme). OneWireNg `OneWire.h` drop-in sağladığı için
     koddaki `#include <OneWire.h>` ve `OneWire oneWire(...)` aynen çalışır.
6. Sketch'i derleyip karta yükleyin; **Serial Monitor**'ı 115200 baud açın.

## Sorun Giderme

### `OneWire_direct_gpio.h: ... 'gpio_dev_t' has no member named 'in1'` derleme hatası

Klasik **OneWire** (Paul Stoffregen) kütüphanesi GPIO'lara doğrudan register
erişimi yapar ve ESP32-H2'nin yeni `gpio_struct.h` yapısı (esp32 core 3.3.x) ile
**uyumsuzdur**. Çözüm:

1. Eski **OneWire** klasörünü **fiziksel olarak silin** (Library Manager'dan
   "Remove" bazen klasörü bırakır):
   ```bash
   rm -rf ~/Arduino/libraries/OneWire
   ```
2. **OneWireNg** (Piotr Stolarz) kurun — ESP32 (classic/S/C/**H**/P) destekler.
3. OneWireNg, Arduino `OneWire` ile API-uyumlu bir `OneWire.h` sağlar; bu yüzden
   sketch kodu **değişmeden** derlenir.

> **Neden silmek şart?** Hem eski `OneWire` hem OneWireNg `OneWire.h` sağlar.
> İkisi birden kuruluyken Arduino IDE, **birebir isim eşleşmesi** nedeniyle
> "OneWire" klasörünü seçip onu derler (hata mesajındaki `.../libraries/OneWire/
> OneWire.cpp` yolu bunu gösterir). Eski klasör silinince geriye yalnızca
> OneWireNg'in başlığı kalır. Doğrulama: `ls ~/Arduino/libraries | grep -i onewire`
> çıktısında sadece `OneWireNg` görünmeli.

## Eşleştirme / Fabrika Ayarı

- İlk açılışta cihaz, **açık (permit-join) bir Zigbee koordinatörü/ağ geçidi**
  (ör. Zigbee2MQTT, Home Assistant ZHA) arar.
- **Fabrika ayarı:** BOOT butonunu (IO9) **5 sn** basılı tutun → röleler
  temizlenir, cihaz ağdan ayrılır ve yeniden başlar.

## Doğrulama Notu

Bu, donanım üzerinde derlenip test edilmesi gereken bir firmware iskeletidir.
Arduino-ESP32 Zigbee API'si sürümler arasında ufak değişiklikler gösterebilir;
derleme hatası alırsanız `ZigbeeLight` / `ZigbeeTempSensor` metod adlarını
kullandığınız çekirdek sürümünün örnekleriyle (File → Examples → Zigbee)
karşılaştırın.

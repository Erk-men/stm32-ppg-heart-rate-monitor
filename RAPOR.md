# BARE-METAL KALP ATIŞ HIZI MONİTÖRÜ
## STM32F070RB Mikrodenetleyici ile PPG Tabanlı Nabız Ölçüm Sistemi

---

**Ders:** İleri Mikrodenetleyiciler  
**Platform:** STM32 Nucleo-F070RB (ARM Cortex-M0)  
**Geliştirme Ortamı:** STM32CubeIDE (HAL kullanılmamış, doğrudan CMSIS kayıt yazımı)

---

## İÇİNDEKİLER

1. Özet
2. Giriş
3. Sistem Mimarisi
4. Donanım Tasarımı
5. Yazılım Mimarisi
6. Sinyal İşleme Algoritması
7. Kalibrasyon ve Test
8. Sonuçlar
9. Tartışma
10. Kaynaklar

---

## 1. ÖZET

Bu rapor, STM32F070RBTx mikrodenetleyicisi üzerinde bare-metal C ile geliştirilmiş yansımalı fotopletismografi (PPG) tabanlı bir kalp atış hızı ölçüm sistemini açıklamaktadır. Sistem; bir kırmızı LED, BPW34 fotodiyot ve LM358 transempedans yükselticisinden oluşan analog ön devre ile STM32'nin dahili ADC'si, donanımsal TIM3 tetikleyicisi ve USART2 seri iletişim modülünü kullanan tam entegre bir çözümdür. HAL veya herhangi bir soyutlama katmanı kullanılmadan, tüm çevre birimler CMSIS kayıt adresleri üzerinden doğrudan yapılandırılmıştır. Geliştirilen algoritma; 32 örnekli hareketli ortalama filtresi, 5 durumlu adaptif eşik tabanlı tepe dedektörü ve 5 örnekli yuvarlanan ortalama BPM hesaplayıcısından oluşmaktadır. Donanım testlerinde BPM değerleri başarıyla ölçülmüş, sonuçlar seri terminal üzerinden doğrulanmıştır.

---

## 2. GİRİŞ

### 2.1 Proje Amacı

Kalp atış hızı (KAH), kardiyovasküler sağlığın en temel göstergelerinden biridir. Bu projede, yüksek seviyeli kütüphaneler veya işletim sistemi kullanılmadan, ARM Cortex-M0 tabanlı bir mikrodenetleyici üzerinde sıfırdan nabız ölçüm sistemi geliştirilmesi hedeflenmiştir.

Temel hedefler:
- Fotopletismografi yöntemiyle optik nabız sinyali elde etmek
- Sinyali sayısal filtreler ile işleyerek gürültüden arındırmak
- Gerçek zamanlı olarak dakika başına atış sayısını (BPM) hesaplamak
- Sonucu USART üzerinden seri terminale iletmek

### 2.2 Fotopletismografi (PPG) Prensibi

Fotopletismografi; doku içindeki kan hacminin optik yollarla ölçülmesi prensibine dayanır. Işık kaynağından gelen ışınlar deri altı dokusuna nüfuz eder ve kılcal damarlar ile arterler içindeki kan tarafından kısmen absorbe edilir, kısmen de yansıtılır.

Kalp her attığında arteriyel kan basıncı artar; bu da doku içindeki kan hacmini artırır. Yansıyan ışık miktarı bu kan hacmi değişimine bağlıdır. BPW34 fotodiyot bu ışık değişimini elektrik akımına çevirir ve LM358 yükselticisi bu akımı ölçülebilir bir gerilime dönüştürür.

```
  KALP ATAR
      |
      v
  Arteriyel kan basıncı artar
      |
      v
  Doku içi kan hacmi artar
      |
      v
  LED ışığının daha fazlası dokuya nüfuz eder / saçılır
      |
      v
  BPW34'e dönen ışık miktarı artar
      |
      v
  Fotodiyot akımı artar → LM358 çıkışı yükselir → ADC değeri yükselir
```

**Önemli Not:** Kırmızı LED (630–700 nm) kullanan yansımalı geometride bu sistem aktif-yüksek (active-high) davranmaktadır. Yani sistolik doruk, ADC değerinin artmasına karşılık gelir. Bu durum, algoritmada sinyal terslemesi gerektirmiş ve donanım testleriyle doğrulanmıştır.

---

## 3. SİSTEM MİMARİSİ

### 3.1 Üst Düzey Blok Diyagramı

```
┌─────────────────────────────────────────────────────────────────┐
│                     ANALOG ÖN DEVRE                             │
│                                                                 │
│   ┌──────────┐    Işık     ┌──────────┐   Akım    ┌─────────┐  │
│   │ Kırmızı  │────────────>│  BPW34   │──────────>│  LM358  │  │
│   │   LED    │   (Parmak) │ Fotodiyot│           │   TIA   │  │
│   └──────────┘             └──────────┘           └────┬────┘  │
│      5V / 470Ω                                         │       │
└────────────────────────────────────────────────────────│───────┘
                                                         │
                                                    Analog Gerilim
                                                    (PA0 / ADC_IN0)
                                                         │
┌────────────────────────────────────────────────────────│───────┐
│                   STM32F070RBTx                         │       │
│                                                        v       │
│  ┌──────────┐  TRGO 100Hz  ┌──────────┐  EOC IRQ  ┌──────┐   │
│  │   TIM3   │─────────────>│   ADC1   │──────────>│ ISR  │   │
│  │ 100 Hz   │              │ CH0/PA0  │           └──┬───┘   │
│  └──────────┘              └──────────┘              │        │
│                                                   g_adc_sample │
│  ┌──────────┐                                        │        │
│  │ SysTick  │ 1ms tick ─── millis()                 v        │
│  └──────────┘                               ┌──────────────┐  │
│                                             │  algorithm_  │  │
│                                             │   process()  │  │
│                                             └──────┬───────┘  │
│                                                    │ BPM       │
│                                             ┌──────v───────┐  │
│                                             │   USART2     │  │
│                                             │  115200 baud │  │
│                                             └──────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                                                    │
                                               Seri Terminal
                                              "BPM: 65"
```

### 3.2 Kesme ve Öncelik Planı

| Kesme       | Öncelik | Gerekçe                                              |
|-------------|---------|------------------------------------------------------|
| SysTick     | 0       | millis() hiçbir zaman gecikmemeli                   |
| ADC1_IRQn   | 1       | 10ms TIM3 tetiklemesinden önce tamamlanmalı          |
| USART2      | —       | Polling ile çalışır, kesme kullanılmaz               |

---

## 4. DONANIM TASARIMI

### 4.1 Bileşen Listesi

| Bileşen     | Değer / Model | Açıklama                                    |
|-------------|---------------|---------------------------------------------|
| MCU         | STM32F070RBTx | ARM Cortex-M0, 128KB Flash, 16KB RAM        |
| Geliştirme kartı | Nucleo-F070RB | ST-Link entegre, USB-UART köprüsü     |
| LED         | Kırmızı (5mm) | ~630nm; orijinal 940nm IR LED arıza yaptı   |
| Fotodiyot   | BPW34         | Geniş açılı Si PIN fotodiyot                |
| Yükselteç   | LM358         | Çift kanallı op-amp, transempedans konfigürasyonu |
| Geri besleme | 1MΩ          | TIA kazancı; I_photo × 1MΩ = V_out         |
| Yanlılık direnci | 2× 10kΩ | Non-inv. giriş için gerilim bölücü          |
| LED akım sınırlama | 470Ω   | 5V / 470Ω ≈ 10.6 mA LED akımı            |

### 4.2 Devre Şeması

```
VCC_5V ──┬─────────────────────────────────────────
         │
        470Ω
         │
         ├──── LED Anot (Kırmızı LED, 5mm)
         │
         │     LED Katot ───────────────────── GND
         │
         │     BPW34 Anot ────────────────── GND
         │
         └──── BPW34 Katot ──────┬──────── LM358 Pin 2 (IN-)
                                 │
                              1MΩ (R_fb)
                                 │
VCC_3V3 ──┬──┐                  └──────── LM358 Pin 1 (OUT) ──── PA0 (ADC_IN0)
          │  │
         10kΩ │
          │  └──────────────────────────── LM358 Pin 3 (IN+)
         10kΩ
          │
         GND

LM358 Pin 8 (VCC) ──── VCC_3V3
LM358 Pin 4 (GND) ──── GND
```

**Not:** BPW34 ters polaritede bağlıdır (fotoiletken mod). Anot GND'ye, katot yükselteç girişine bağlıdır. Fotodiyot akımı 1MΩ geri besleme direnci üzerinden LM358 çıkışına yansıtılır.

### 4.3 LM358 Transempedans Yükselticisi (TIA)

TIA konfigürasyonunda LM358 şu işlevi yerine getirir:

```
          R_fb = 1MΩ
    ┌──────────────────┐
    │                  │
IN- ┤ (-)              │
    │         LM358    ├──── V_out = I_photo × R_fb
IN+ ┤ (+) Vbias        │
    │                  │
    └──────────────────┘

V_bias = VCC × (R2/(R1+R2)) = 3.3V × (10k/(10k+10k)) = 1.65V
```

Gerilim bölücü, LM358'in non-inverting girişini 1.65V'a sabitler. Bu, ADC'nin tam ölçüm aralığını kullanmasını sağlar.

### 4.4 Sinyal Özellikleri (Donanım Ölçümleri)

CALIBRATION_MODE ile elde edilen gerçek donanım ölçümleri:

| Durum              | Min ADC | Max ADC | Genlik |
|--------------------|---------|---------|--------|
| Parmak yok         | 1954    | 2063    | 109    |
| Parmak var         | 1816    | 2131    | 315    |

- **DC ofset (parmak var):** ~2078 ADC birimi (12-bit ölçek: 0–4095)
- **AC bileşen:** 315 ADC birimi (~7.7% dinamik aralık)
- **Sinyal yönü:** Aktif-yüksek (sistolik doruk → ADC artar)

---

## 5. YAZILIM MİMARİSİ

### 5.1 Dosya Yapısı

```
HeartRateSensor1/
├── Src/
│   ├── main.c          — Başlatma ve ana döngü
│   ├── algorithm.c     — Sinyal işleme ve BPM hesaplama
│   ├── adc.c           — ADC1 donanım sürücüsü
│   ├── tim3.c          — TIM3 100Hz tetikleyici
│   ├── usart.c         — USART2 seri çıkış
│   └── systick.c       — 1ms millis() sayacı
├── Inc/
│   ├── algorithm.h
│   ├── adc.h
│   ├── tim3.h
│   ├── usart.h
│   └── systick.h
└── Drivers/
    └── CMSIS/           — ST tarafından sağlanan başlık dosyaları
```

**Tasarım ilkesi:** algorithm.c hiçbir çevre birimi kaydına erişmez. Donanım sürücüleri yalnızca kendi modüllerinde bulunur. Bu ayrım, hem test edilebilirliği artırır hem de derleyicinin bağımlılıkları net görmesini sağlar.

### 5.2 Saat Ağacı Yapılandırması

```
HSI 8MHz (dahili RC osilatör)
      │
      │  SystemInit() stub — PLL devre dışı
      │
      ├── HCLK = 8 MHz
      ├── PCLK = 8 MHz (tek APB, F070'te APB çarpanı yok)
      ├── TIM3 clock = 8 MHz
      ├── USART2 clock = 8 MHz → BRR = 0x45 (115200 baud)
      └── SysTick = 8 MHz → LOAD = 7999 (1ms tick)

HSI14 (14 MHz — ADC'ye özel RC osilatör)
      └── ADC1 clock (F070'e özgü)
```

### 5.3 GPIO Pin Haritası

| Pin | Fonksiyon    | MODER | AFR       | Açıklama                      |
|-----|--------------|-------|-----------|-------------------------------|
| PA0 | ADC_IN0      | 11    | —         | Analog mod, PPG sinyali girişi |
| PA2 | USART2_TX    | 10    | AF1       | Seri çıkış (ST-Link'e)        |
| PA3 | USART2_RX    | 10    | AF1       | Seri giriş (kullanılmıyor)    |

### 5.4 TIM3 — 100Hz ADC Tetikleyici

```c
// 8 MHz / (PSC+1) / (ARR+1) = 100 Hz
TIM3->PSC = 79;   // 8MHz / 80 = 100kHz sayaç saati
TIM3->ARR = 999;  // 100kHz / 1000 = 100Hz güncelleme olayı
TIM3->CR2 = TIM_CR2_MMS_1;  // TRGO = Update Event (MMS=010)
```

TIM3, her 10ms'de bir TRGO sinyali üretir. Bu sinyal ADC1'i doğrudan tetikler; yazılım müdahalesi gerekmez.

### 5.5 ADC1 — Donanım Tetiklemeli, Kesme Tabanlı

```c
ADC1->CFGR1 = ADC_CFGR1_EXTSEL_0 | ADC_CFGR1_EXTSEL_1  // EXTSEL=011: TIM3_TRGO
            | ADC_CFGR1_EXTEN_0                           // Yükselen kenar tetikleme
            | ADC_CFGR1_OVRMOD;                           // Taşma: DR üzerine yaz
ADC1->SMPR  = ADC_SMPR_SMP;                              // Maks. örnekleme süresi (SNR için)
ADC1->CHSELR = ADC_CHSELR_CHSEL0;                        // Kanal 0 (PA0)
ADC1->IER   = ADC_IER_EOCIE;                             // EOC kesmesi etkin
```

ADC kalibrasyonu zorunludur (F070'e özgü gereksinim):

```c
ADC1->CR |= ADC_CR_ADCAL;
while (ADC1->CR & ADC_CR_ADCAL);  // Kalibrasyon tamamlanana kadar bekle
```

### 5.6 ADC Kesme Servis Rutini (ISR)

```c
void ADC_IRQHandler(void)
{
    if (ADC1->ISR & ADC_ISR_EOC) {
        g_adc_sample = (uint16_t)(ADC1->DR & 0x0FFF);
        g_adc_ready  = 1;
    }
}
```

ISR yalnızca iki volatile değişkeni günceller ve döner. Tüm sinyal işleme ana döngüde gerçekleşir.

### 5.7 Ana Döngü

```c
while (1) {
    if (g_adc_ready) {
        g_adc_ready = 0;
        algorithm_process((uint16_t)g_adc_sample);
    }
}
```

Ana döngü, g_adc_ready bayrağını yoklar. Her yeni örnek için algorithm_process() çağrılır. Kesme ile ana döngü arasındaki iletişim iki volatile değişken üzerinden gerçekleşir.

---

## 6. SİNYAL İŞLEME ALGORİTMASI

### 6.1 Sinyal Terslemesi

Kırmızı LED kullanan yansımalı PPG devresinde sinyal aktif-yüksek davranmaktadır (sistolik doruk → ADC artar). Standart tepe dedektörü algoritması yükselen tepe araması yapar; bu nedenle girdi sinyali terslenmiştir:

```c
uint16_t inv = (uint16_t)(4095u - sample);
```

| Ölçüm          | Ham ADC | Terslenmiş ADC |
|----------------|---------|----------------|
| DC taban       | ~2078   | ~2017          |
| Sistolik doruk | ~2131   | ~1964          |
| Minimum        | ~1816   | ~2279          |

Terslemeden sonra diyastol minimumu (1816) → terslenmiş maksimum (2279) olur ve algoritma bu değeri "tepe" olarak algılar.

### 6.2 32 Örnekli Hareketli Ortalama Filtresi

```
          x[n] + x[n-1] + ... + x[n-31]
y[n]  =  ────────────────────────────────
                        32
```

Uygulama; dairesel tampon ve koşan toplam kullanılarak O(1) karmaşıklıkla gerçekleştirilmiştir:

```c
s_ma_sum -= s_ma_buf[s_ma_idx];   // En eski örneği çıkar
s_ma_buf[s_ma_idx] = inv;          // Yeni terslenmiş örneği ekle
s_ma_sum += inv;
s_ma_idx = (s_ma_idx + 1) & 0x1F; // Bölümsüz modülo 32
uint16_t filtered = (uint16_t)(s_ma_sum >> 5); // Bölümsüz /32
```

**Frekans yanıtı:** 100Hz örnekleme frekansında 32 örneklik pencere, 3.125Hz üzerindeki frekanslarda zayıflatma sağlar. 1Hz'deki (60 BPM) nabız sinyali için zayıflatma ~%16'dır (0.84 kazanç), bu da tepe tespiti için yeterlidir.

### 6.3 5 Durumlu Adaptif Eşikli Tepe Dedektörü

```
                     filtered > threshold
    ┌──────┐  ───────────────────────────>  ┌────────┐
    │      │                                │        │
    │ IDLE │                                │ RISING │
    │      │  <───────────────────────────  │        │
    └──────┘       filtered <= threshold    └────┬───┘
        ^           (FALLING'den)                │
        │                                  peak - filtered >= 20
        │                                        │
        │                                        v
    ┌──────────┐                          ┌────────────┐
    │          │                          │            │
    │ FALLING  │                          │ PEAK_HOLD  │
    │          │   <──────────────────── │            │
    └──────────┘   refractory süresi      └────────────┘
        ^          dolunca                      │
        │                                 threshold güncelle
        │                                 BPM hesapla
        │                                       │
        │                               ┌───────────────┐
        └───────────────────────────── │  REFRACTORY   │
                                       │   (350 ms)    │
                                       └───────────────┘
```

#### Durum Açıklamaları

| Durum       | Koşul / Eylem                                                 |
|-------------|---------------------------------------------------------------|
| IDLE        | filtered > threshold olana kadar bekle → RISING              |
| RISING      | Maksimumu izle; tepe 20 birim düştüğünde → PEAK_HOLD         |
| PEAK_HOLD   | BPM hesapla, eşiği güncelle, refrakter zamanlayıcıyı başlat  |
| REFRACTORY  | 350ms bekle (dikrotik çentik bastırma) → FALLING             |
| FALLING     | filtered ≤ threshold olana kadar bekle → IDLE                |

#### REFRACTORY Durumunun Önemi

Kalp atışının ardından oluşan **dikrotik çentik** (dicrotic notch), dalga biçiminde ikincil bir tepe oluşturur. Bu sahte tepin çift vuruş olarak algılanmasını önlemek için 350ms refrakter pencere uygulanmıştır. Bu değer, fizyolojik 300–400ms refrakter penceresinin orta noktasıdır.

```
  ADC    ┌─ Sistolik doruk
         │  ┐
2279 ──  │  └─┐  ← Dikrotik çentik (REFRACTORY tarafından bastırılır)
         │    └───────────────────────────────── DC taban
2150 ──  ┼ ─ ─ ─ ─ Eşik (threshold = 2150)
2078 ──  ───────────────────────────────────────
         │
         └─────────────────────────────────> Zaman

         │←──── RISING ────→│← PEAK_HOLD →│←─ REFRACTORY (350ms) ─→│← FALLING →│
```

### 6.4 Eşik Değeri

Donanım kalibrasyonuna dayalı sabit eşik değeri:

| Ölçüm                          | Değer |
|--------------------------------|-------|
| Terslenmiş DC taban (dinlenim) | 2078  |
| Terslenmiş sistolik doruk      | 2279  |
| Seçilen eşik                   | 2150  |
| DC taban üstü marj             | 72 birim |
| Doruk altı marj                | 129 birim |

Eşik 2150, DC taban ile doruk arasındaki aralığın ortasına yakın seçilmiştir. Bu; yanlış tetiklemelere karşı 72 birimlik emniyet marjı ve gerçek nabız tespiti için 129 birimlik marj sağlar.

### 6.5 BPM Hesaplama ve Yuvarlanan Ortalama

```
              60000
BPM  =  ─────────────────
         interval_ms

interval_ms = t_tepe[n] - t_tepe[n-1]    (millis() ile ölçülür)
```

Fizyolojik sınır kontrolü: yalnızca 40–200 BPM aralığındaki değerler kabul edilir.

5 örneklik yuvarlanan ortalama ile gürültü azaltması:

```c
s_bpm_buf[s_bpm_idx] = (uint16_t)bpm;
s_bpm_idx = (s_bpm_idx + 1) % 5;
if (s_bpm_count < 5) s_bpm_count++;

uint32_t sum = 0;
for (uint8_t i = 0; i < s_bpm_count; i++) sum += s_bpm_buf[i];
uint32_t bpm_avg = sum / s_bpm_count;
```

---

## 7. KALİBRASYON VE TEST

### 7.1 CALIBRATION_MODE

Başlatma sırasında etkinleştirilen CALIBRATION_MODE, 5 saniye boyunca ham ADC örneklerinin minimum ve maksimum değerlerini ölçer:

```
CALIBRATION_MODE: sampling 5s...
CAL min=1816 max=2131 amp=315 n=501
```

501 örnek ÷ 5000ms = 100.2 Hz — TIM3'ün 100Hz tetiklemesi doğrulanmıştır.

### 7.2 Aşamalı Geliştirme Süreci

| Aşama | Hedef                                              | Sonuç        |
|-------|----------------------------------------------------|--------------|
| 1     | SysTick, USART2 kurulumu, "Hello World"            | Başarılı     |
| 2     | TIM3 + ADC zinciri, sentetik PPG testi             | Başarılı     |
| 3     | Algoritma doğrulaması (sentetik sinyal ile)        | Başarılı     |
| 4     | Gerçek devre entegrasyonu, analog kalibrasyon      | Başarılı     |

### 7.3 Karşılaşılan Donanım Sorunları

| Sorun                                   | Teşhis                                | Çözüm                              |
|-----------------------------------------|---------------------------------------|------------------------------------|
| IR LED çalışmıyor                       | LED arızalı (izole test ile doğrulandı) | Kırmızı LED ile değiştirildi     |
| Genlik = 25 (beklenen: >200)            | LED ve BPW34 aralarında 7 sıra mesafe | Aynı satıra yerleştirildi, genlik 315'e yükseldi |
| LED yanmıyor                            | Aynı breadboard satırı kısa devre oluşturdu | LED bacakları farklı satırlara taşındı |
| FALLING durumu hiç çıkmıyor             | Eşik, DC ofsetin altında kalıyordu    | Donanım ölçümüyle eşik kalibrasyonu yapıldı |
| Sinyal terslenmiş davranıyor            | Kırmızı LED PPG aktif-yüksek          | Algoritmaya 4095-sample terslemesi eklendi |

### 7.4 Donanım Test Sonuçları

Temiz temas ve sabit parmak pozisyonuyla elde edilen temsili ölçümler:

| Ölçüm No | BPM  |
|----------|------|
| 1        | 44   |
| 2        | 56   |
| 3        | 52   |
| 4        | 54   |
| 5        | 51   |
| 6        | 52   |
| 7        | 50   |

Ortalama: **51.3 BPM** — dinlenme halindeki normal KAH aralığına yakın değer.

---

## 8. SONUÇLAR

- STM32F070RBTx üzerinde, HAL kullanılmadan, tüm çevre birimler CMSIS kayıtları aracılığıyla başarıyla yapılandırılmıştır.
- TIM3 → ADC1 donanım tetikleme zinciri, yazılım müdahalesi olmaksızın 100Hz örnekleme sağlamıştır.
- 32 örnekli hareketli ortalama filtresi ve 5 durumlu adaptif eşik dedektörü gerçek zamanlı nabız tespiti gerçekleştirmiştir.
- Donanım kalibrasyonu ile elde edilen eşik değeri (2150), kararlı durum tespiti için yeterli marj sağlamıştır.
- Kırmızı LED kullanımının gerektirdiği sinyal terslemesi, donanım ölçümleriyle teşhis edilerek algoritmaya entegre edilmiştir.
- BPM değerleri USART2 üzerinden seri terminale başarıyla iletilmiştir.

---

## 9. TARTIŞMA

### 9.1 HAL Yerine Bare-Metal Yaklaşımı

Bu projede HAL (Hardware Abstraction Layer) kütüphanesi kasıtlı olarak kullanılmamıştır. Her çevre birimi, referans kılavuzundan (RM0091) alınan kayıt adresleri kullanılarak doğrudan yapılandırılmıştır. Bu yaklaşım:

- Donanım davranışının tam olarak anlaşılmasını zorunlu kılmıştır.
- F070'e özgü farklılıkların (HSI14 ADC saati, tek APB, MODER/AFR GPIO yapılandırması, ISR/TDR USART kayıtları) öğrenilmesini sağlamıştır.
- Kod boyutunu ve gecikmeyi minimize etmiştir.

### 9.2 Kısıtlamalar

**Fiziksel temas kararsızlığı:** Breadboard üzerinde sensörle temas, ölçüm süresince parmağın sabit tutulmasını gerektirmektedir. Küçük hareketler sinyal genliğini önemli ölçüde değiştirmektedir.

**Sabit eşik:** Mevcut implementasyonda eşik değeri, bu devreye özgü donanım kalibrasyonuyla belirlenmiştir. Farklı devre geometrileri veya farklı kişiler için yeniden kalibrasyon gerekebilir.

**Kırmızı LED yerine IR LED:** Orijinal 940nm IR LED arızası nedeniyle kırmızı LED ile çalışılmıştır. IR LED, daha derin doku penetrasyonu ve daha az ortam ışığı etkileşimi sağladığından daha iyi SNR verir.

### 9.3 Gelecek İyileştirmeler

- **Adaptif eşik:** DC taban seviyesini dinamik olarak izleyen algoritma
- **DC bloklama filtresi:** Sabit eşik yerine AC bileşen üzerinde çalışan yaklaşım
- **Parmak tespiti:** Genlik eşiği ile parmak varlığının otomatik algılanması
- **SpO2 ölçümü:** İkinci bir dalga boyu (IR) ile oksijen doygunluğu hesaplama

---

## 10. KAYNAKLAR

1. STMicroelectronics, *STM32F070xB Reference Manual RM0091*, Rev. 9 — Tüm kayıt adresleri ve zamanlama hesaplamaları için birincil kaynak.

2. STMicroelectronics, *STM32F070RB Datasheet* — Pin çoklama tablosu, ADC kanal haritalaması.

3. ARM Limited, *Cortex-M0 Technical Reference Manual* — SysTick_Config(), NVIC_SetPriority(), NVIC_EnableIRQ() fonksiyon imzaları (core_cm0.h).

4. STMicroelectronics, *Nucleo-F070RB User Manual (UM1724)* — PA2/PA3'ün ST-Link sanal COM portuna yönlendirilmesi, HSE kristal konfigürasyonu.

5. J. Allen, "Photoplethysmography and its application in clinical physiological measurement," *Physiological Measurement*, vol. 28, no. 3, 2007 — PPG sinyal özellikleri ve fizyolojik arka plan.

---

*Bu rapor, gerçek donanım üzerinde gerçekleştirilen geliştirme ve test sürecine dayanmaktadır. Tüm ölçümler, Nucleo-F070RB geliştirme kartına bağlı breadboard devresi üzerinde alınmıştır.*

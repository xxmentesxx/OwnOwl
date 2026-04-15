/**
 * ============================================================
 *  OWNOWL V1.01 — ANA MODÜL (ESP32-S3)
 * ============================================================
 *  Çift Ekran + Motor + Ses + Yüz Takibi
 *  
 *  Kamera UART: ESP32-CAM UOT → ESP32-S3 RX (GPIO 44)
 *  Protokol: "F:x,y,w,h\n" (yüz var) / "N\n" (yüz yok)
 * ============================================================
 */

#include <LovyanGFX.hpp>
#include <Stepper.h>
#include <driver/i2s.h>
#include <math.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// --- 1. WI-FI BİLGİLERİ ---
const char* ssid = "COMOLOKKO 2.4";
const char* password = "papipapi-135246";

// --- 2. DONANIM PİNLERİ ---
// EKRAN (Pin 1-5)
#define PIN_SCLK 1
#define PIN_MOSI 2
#define PIN_RST  3
#define PIN_DC   4
#define PIN_BLK  5

// SES (Pin 6-9)
#define I2S_SCK  6 
#define I2S_WS   7 
#define I2S_SD   8  
#define I2S_DIN  9  

// MOTOR (Pin 10-13)
#define IN1 10
#define IN3 11
#define IN2 12
#define IN4 13

// KAMERA UART (UART0 on S3, USB-CDC frees these pins)
#define CAM_RX_PIN 44  // ESP32-CAM TX → buraya bağlı
#define CAM_TX_PIN -1   // Bağlı değil, tek yönlü

// --- 3. MOTOR AYARLARI ---
const int stepsPerRevolution = 2048;
const int MOTOR_MAX_STEPS    = 1024;   // +180° limit (steps)
const int MOTOR_MIN_STEPS    = -1024;  // -180° limit (steps)
const int MOTOR_WRAP_LIMIT   = 1138;   // ~200° (steps) — bu üzerinde ters yönden git
const int MOTOR_MAX_STEP_PER_LOOP = 100; // Döngü başına max adım
const int MOTOR_MIN_STEP         = 30;   // Minimum adım (bunun altı dişlide titreşim olarak kalır)
const int MOTOR_DEADZONE         = 40;   // ±40 piksel hata → hareket etme

// --- 4. KAMERA AYARLARI ---
#define FRAME_W 320
#define FRAME_H 240
// Kamera ters monte edildiyse bunu true yap
#define CAMERA_MIRROR_X false
#define CAMERA_MIRROR_Y false

// --- EKRAN SINIFI ---
class LGFX_S3Mini : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;
public:
  LGFX_S3Mini() {
    auto cfg = _bus_instance.config();
    cfg.spi_host = SPI2_HOST; 
    cfg.spi_mode = 0; 
    cfg.freq_write = 40000000; 
    cfg.pin_sclk = PIN_SCLK; cfg.pin_mosi = PIN_MOSI; cfg.pin_miso = -1; cfg.pin_dc = PIN_DC;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);
    auto pcfg = _panel_instance.config();
    pcfg.pin_cs = -1; pcfg.pin_rst = -1;
    pcfg.panel_width = 240; pcfg.panel_height = 240; pcfg.invert = true;
    _panel_instance.config(pcfg);
    setPanel(&_panel_instance);
  }
};

LGFX_S3Mini tft;
LGFX_Sprite sprite(&tft);
Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);

// --- KAMERA UART ---
// UART1 kullanıyoruz. UART0, USB-CDC aktifken bile ESP32-S3'te
// dahili boot log/console ile çakışabiliyor.
HardwareSerial CamSerial(1); // UART1 — GPIO Matrix ile pin 44'e atanacak

// --- DURUM DEĞİŞKENLERİ (cross-core, volatile) ---
// Göz hedefleri
volatile int targetLookX = 120;
volatile int targetLookY = 120;
volatile bool isAngry    = false;
volatile bool blinkNow   = false;

// Yüz verisi (kameradan)
volatile int  faceX = FRAME_W / 2;  // Merkez varsayılan
volatile int  faceY = FRAME_H / 2;
volatile int  faceW = 0;
volatile int  faceH = 0;
volatile bool faceDetected = false;
volatile unsigned long lastFaceTime = 0;
volatile unsigned long lastCamRxTime = 0; // Son UART verisi zamanı (F: veya N fark etmez)

// Motor pozisyon takibi
int motorPosition = 0; // Adım cinsinden mevcut pozisyon (0 = merkez)

// FreeRTOS
TaskHandle_t Task_EyeAnimation;

// =============================================================
//  KAMERA UART PARSER — Akıllı Filtreleme (S3 tarafı)
// =============================================================
// Kamera ham veri gönderir, S3 neyin gerçek yüz olduğuna karar verir.
// Hassasiyet ayarları aşağıdaki sabitlerden yapılır.
// =============================================================

String camBuffer = "";

// --- FİLTRELEME PARAMETRELERİ (ayar için burayı değiştir) ---
const int MIN_FACE_SIZE     = 20;   // Bundan küçük tespit = gürültü
const int MAX_FACE_SIZE     = 250;  // Bundan büyük tespit = hata
const int MAX_JUMP_PIXELS   = 100;  // Tek karede max sıçrama (üstü = yanlış pozitif)
const int CONFIRM_FRAMES    = 3;    // Bu kadar ardışık F: = gerçek yüz
const int LOSE_FRAMES       = 10;   // Bu kadar ardışık N = yüz kayboldu

// --- FİLTRE DURUM ---
int consecutiveF = 0;
int consecutiveN = 0;
int lastRawX = FRAME_W / 2;
int lastRawY = FRAME_H / 2;

void parseCameraData() {
    static unsigned long lastRxLog = 0;
    
    while (CamSerial.available()) {
        char c = CamSerial.read();
        
        if (c == '\n') {
            camBuffer.trim();
            lastCamRxTime = millis();
            
            if (camBuffer.length() >= 5 && 
                (camBuffer[0] == 'F' || camBuffer[0] == 'M') && camBuffer[1] == ':') {
                // F: = yüz tespiti, M: = hareket tespiti — ikisi de aynı şekilde işlenir
                int vals[4] = {0};
                int parsed = sscanf(camBuffer.c_str() + 2, "%d,%d,%d,%d", 
                                    &vals[0], &vals[1], &vals[2], &vals[3]);
                
                if (parsed == 4) {
                    int fx = vals[0];
                    int fy = vals[1];
                    int fw = vals[2];
                    int fh = vals[3];

                    if (CAMERA_MIRROR_X) fx = FRAME_W - fx;
                    if (CAMERA_MIRROR_Y) fy = FRAME_H - fy;

                    // FİLTRE 1: Boyut
                    bool sizeOK = (fw >= MIN_FACE_SIZE && fh >= MIN_FACE_SIZE && 
                                   fw <= MAX_FACE_SIZE && fh <= MAX_FACE_SIZE);

                    // FİLTRE 2: Sıçrama
                    int jumpX = abs(fx - lastRawX);
                    int jumpY = abs(fy - lastRawY);
                    bool jumpOK = !faceDetected || (jumpX <= MAX_JUMP_PIXELS && jumpY <= MAX_JUMP_PIXELS);

                    if (sizeOK && jumpOK) {
                        consecutiveF++;
                        consecutiveN = 0;
                        lastRawX = fx;
                        lastRawY = fy;

                        if (consecutiveF >= CONFIRM_FRAMES) {
                            faceX = fx;
                            faceY = fy;
                            faceW = fw;
                            faceH = fh;
                            faceDetected = true;
                            lastFaceTime = millis();
                        }
                    }
                }
            }
            else if (camBuffer.length() > 0 && camBuffer[0] == 'N') {
                consecutiveN++;
                consecutiveF = 0;
                if (consecutiveN >= LOSE_FRAMES) {
                    faceDetected = false;
                }
            }
            
            camBuffer = "";
        }
        else if (c != '\r') {
            camBuffer += c;
            if (camBuffer.length() > 60) {
                camBuffer = "";
            }
        }
    }
    
    // Her 5 saniyede istatistik
    if (millis() - lastRxLog > 5000) {
        Serial.printf("[FILTER] ArdF:%d ArdN:%d Yuz:%s\n", 
            consecutiveF, consecutiveN,
            faceDetected ? "EVET" : "HAYIR");
        lastRxLog = millis();
    }
}

// =============================================================
//  MOTOR KONTROL — Bobinleri söndürme + Pozisyon takipli hareket
// =============================================================

// Bobinleri söndür — motor durduğunda ısınmayı önler
void disableMotor() {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
}

void moveMotor(int steps) {
    if (steps == 0) return;
    
    int newPos = motorPosition + steps;
    
    // ±180° sert limit
    newPos = constrain(newPos, MOTOR_MIN_STEPS, MOTOR_MAX_STEPS);
    
    int actualSteps = newPos - motorPosition;
    
    // 200° üzeri tek seferde dönüş gerekiyorsa → ters yönden git
    if (abs(actualSteps) > MOTOR_WRAP_LIMIT) {
        if (actualSteps > 0) {
            actualSteps = actualSteps - stepsPerRevolution;
        } else {
            actualSteps = actualSteps + stepsPerRevolution;
        }
        newPos = motorPosition + actualSteps;
        newPos = constrain(newPos, MOTOR_MIN_STEPS, MOTOR_MAX_STEPS);
        actualSteps = newPos - motorPosition;
    }
    
    // Döngü başına maximum adım limiti (bloklama süresini sınırla)
    actualSteps = constrain(actualSteps, -MOTOR_MAX_STEP_PER_LOOP, MOTOR_MAX_STEP_PER_LOOP);
    
    if (actualSteps != 0) {
        myStepper.step(actualSteps);
        motorPosition += actualSteps;
    }
    
    // Hareket bitti → bobinleri söndür (ısınmayı önle)
    disableMotor();
}

// Motor pozisyonunu sıfıra döndür (kademeli)
void returnMotorToCenter() {
    if (motorPosition > 0) {
        moveMotor(-min(15, motorPosition));
    } else if (motorPosition < 0) {
        moveMotor(min(15, -motorPosition));
    }
}

// =============================================================
//  YÜZ TAKİBİ — Yumuşatılmış, organik hareket
// =============================================================

// EMA (Exponential Moving Average) filtresi
// Kameradan gelen ham koordinatları yumuşatır, titreme önler
float smoothFaceX = 160.0; // Başlangıç: kamera merkezi
float smoothFaceY = 120.0;
const float EMA_ALPHA = 0.25; // 0.0=donuk, 1.0=anlık. 0.25 = dengeli

void trackFace() {
    if (!faceDetected) return;
    
    // --- YUMUSATMA FİLTRESİ ---
    // Ham kamera verisini direkt kullanma, EMA ile yumuşat
    smoothFaceX = smoothFaceX + EMA_ALPHA * ((float)faceX - smoothFaceX);
    smoothFaceY = smoothFaceY + EMA_ALPHA * ((float)faceY - smoothFaceY);
    
    // --- GÖZ TAKİBİ ---
    // Yumuşatılmış koordinatları göz iris alanına eşle
    targetLookX = map((int)smoothFaceX, 0, FRAME_W, 45, 195);
    targetLookY = map((int)smoothFaceY, 0, FRAME_H, 55, 185);
    
    // --- MOTOR TAKİBİ ---
    int errorX = (int)smoothFaceX - (FRAME_W / 2);
    
    if (abs(errorX) > MOTOR_DEADZONE) {
        int steps = -(errorX / 3);
        // Minimum adım: dişlide görünür hareket için
        if (abs(steps) < MOTOR_MIN_STEP) {
            steps = (steps > 0) ? MOTOR_MIN_STEP : -MOTOR_MIN_STEP;
        }
        steps = constrain(steps, -100, 100);
        moveMotor(steps);
    }
}

// =============================================================
//  OTA KURULUMU
// =============================================================
void setupOTA() {
    // Ekranı HEMEN başlat (WiFi'den önce)
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Baslatiliyor...", 40, 110);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);  // Arka planda yeniden bağlanır
    WiFi.persistent(true);
    WiFi.begin(ssid, password);
    
    // Sadece 3 saniye bekle, bağlanamazsa devam et
    int tryCount = 0;
    while (WiFi.status() != WL_CONNECTED && tryCount < 6) {
        delay(500);
        tryCount++;
    }

    ArduinoOTA.setHostname("OwnOwl");
    ArduinoOTA.onStart([]() {
        // OTA başladığında her şeyi durdur
        isAngry = false;
        disableMotor();
        Serial.println("[OTA] Guncelleme basliyor...");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA] Guncelleme tamamlandi. Yeniden baslatiliyor...");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] %%%u\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Hata[%u]\n", error);
    });
    ArduinoOTA.begin();
    
    if (WiFi.status() == WL_CONNECTED) {
        tft.fillScreen(TFT_BLACK);
        tft.drawString("WiFi OK!", 80, 100);
        tft.drawString(WiFi.localIP().toString(), 60, 120);
        delay(2000);
    } else {
        tft.fillScreen(TFT_RED);
        tft.drawString("WiFi YOK!", 80, 110);
        delay(2000);
    }
}

// =============================================================
//  SES AYARLARI
// =============================================================
void setupAudio() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num   = I2S_SCK,
        .ws_io_num    = I2S_WS,
        .data_out_num = I2S_DIN,
        .data_in_num  = I2S_SD
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_set_clk(I2S_NUM_0, 44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

void playTonePacket(int freq, int duration_ms, int volume) {
    size_t bytes_written;
    int num_samples = (44100 * duration_ms) / 1000;
    int16_t *samples = (int16_t *)malloc(num_samples * sizeof(int16_t));
    if (samples == NULL) return;

    for (int i = 0; i < num_samples; i++) {
        double t = (double)i / 44100.0;
        double envelope = 1.0 - ((double)i / num_samples);
        double val = sin(2 * M_PI * freq * t);
        samples[i] = (int16_t)(val * volume * envelope);
    }
    i2s_write(I2S_NUM_0, samples, num_samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    free(samples);
}

void playHootSound() {
    playTonePacket(400, 200, 15000);
    delay(50);
    playTonePacket(350, 600, 15000);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

// =============================================================
//  GÖZ ANİMASYONU — Core 0 (FreeRTOS Task)
// =============================================================
void TaskEyeCode(void * pvParameters) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    sprite.createSprite(240, 240);
    sprite.setSwapBytes(true);
    
    int currentX = 120;
    int currentY = 120;
    float eyeOpen = 1.0;

    for (;;) {
        // Yumuşak geçiş (lerp) — organik ama duyarlı
        int lerpSpeed = isAngry ? 3 : 6;
        currentX += (targetLookX - currentX) / lerpSpeed;
        currentY += (targetLookY - currentY) / lerpSpeed;

        // Kırpma animasyonu
        if (blinkNow) {
            eyeOpen -= 0.2;
            if (eyeOpen <= 0) { eyeOpen = 0; blinkNow = false; }
        } else if (eyeOpen < 1.0) {
            eyeOpen += 0.05;
        }

        // --- Çiz ---
        sprite.fillScreen(TFT_BLACK);

        uint16_t scleraColor = isAngry ? TFT_RED : TFT_WHITE;
        int eyeH = (int)(110 * eyeOpen);

        // Sklera (beyaz göz küresi)
        sprite.fillEllipse(120, 120, 110, eyeH, scleraColor);

        // İris + göz bebeği + parlama (göz yeterince açıksa)
        if (eyeOpen > 0.2) {
            // 3 kademeli iris rengi:
            // BEYAZ  = Kameradan veri gelmiyor
            // SARI   = Kamera bağlı ama yüz bulamıyor
            // YEŞİL  = Yüz tespit edildi, takip aktif
            bool camAlive = (millis() - lastCamRxTime < 2000) && (lastCamRxTime > 0);
            uint16_t irisColor;
            if (camAlive && faceDetected) {
                irisColor = TFT_GREEN;   // Yüz bulundu
            } else if (camAlive) {
                irisColor = TFT_YELLOW;  // Kamera var, yüz yok
            } else {
                irisColor = TFT_WHITE;   // Kamera bağlantısı yok
            }
            
            sprite.fillCircle(currentX, currentY, 35, irisColor);
            sprite.fillCircle(currentX, currentY, 18, TFT_BLACK);
            sprite.fillCircle(currentX + 8, currentY - 8, 6, TFT_WHITE);
        }

        // Kızgın kaş
        if (isAngry) {
            sprite.fillTriangle(0, 0, 240, 0, 120, 90, TFT_BLACK);
        }

        sprite.pushSprite(0, 0);
        vTaskDelay(30 / portTICK_PERIOD_MS); // ~33 FPS
    }
}

// =============================================================
//  KIZGIN MOD — "Ne oluyor?" arama sekansı
// =============================================================
// Motor + göz eş zamanlı sağa-sola bakarıp araştırır
int angryCount = 0;                  // Ardışık kızgınlık sayacı
unsigned long angryCooldownEnd = 0;   // Bekleme süresi bitmeden tetikleme
unsigned long firstAngryTime = 0;    // İlk kızgınlığın zamanı

void angrySequence() {
    isAngry = true;
    
    playHootSound();

    // 1. SOLA BAK (göz + motor)
    targetLookX = 45;
    targetLookY = 140;
    moveMotor(-1500);
    delay(600);

    // 2. SAĞA BAK
    targetLookX = 195;
    targetLookY = 100;
    moveMotor(3000);
    delay(600);

    // 3. TEKRAR SOLA
    targetLookX = 60;
    targetLookY = 130;
    moveMotor(-2000);
    delay(500);

    // 4. MERKEZE DÖN
    targetLookX = 120;
    targetLookY = 120;
    moveMotor(500); // yaklaşık merkeze
    delay(800);

    isAngry = false;
    targetLookX = 120;
    targetLookY = 120;

    i2s_zero_dma_buffer(I2S_NUM_0);
    disableMotor();
}

// =============================================================
//  SETUP
// =============================================================
void setup() {
    // USB-CDC Serial (debug)
    Serial.begin(115200);
    
    // Kamera UART (UART1, GPIO Matrix ile RX=44'e atanıyor)
    CamSerial.begin(115200, SERIAL_8N1, CAM_RX_PIN, -1);
    CamSerial.setTimeout(5);
    
    Serial.println("OwnOwl: Boot...");
    Serial.println("UART1 baslatildi, RX=GPIO44");

    // Ekran donanım reset
    pinMode(PIN_BLK, OUTPUT);
    digitalWrite(PIN_BLK, HIGH);
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, HIGH); delay(50);
    digitalWrite(PIN_RST, LOW);  delay(100);
    digitalWrite(PIN_RST, HIGH); delay(100);

    // OTA + WiFi
    setupOTA();
    
    // Motor
    myStepper.setSpeed(12);
    
    // Ses
    setupAudio();

    // Göz animasyonu → Core 0
    xTaskCreatePinnedToCore(TaskEyeCode, "EyeTask", 10000, NULL, 1, &Task_EyeAnimation, 0);

    // OTA task → Core 0 (motor bloklamasından bağımsız çalışır)
    xTaskCreatePinnedToCore([](void*) {
        for (;;) {
            ArduinoOTA.handle();
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }, "OTATask", 4096, NULL, 2, NULL, 0);  // Core 0, öncelik 2 (gözden yüksek)

    Serial.println("OwnOwl: Sistem Hazir. Kamera bekleniyor...");
    playHootSound();
}

// =============================================================
//  ANA DÖNGÜ — Core 1
// =============================================================
void loop() {
    // OTA artık ayrı task'ta, burada çağırmaya gerek yok

    // --- 1. KAMERA VERİSİ OKU (non-blocking) ---
    parseCameraData();

    // --- 2. MİKROFON DİNLE ---
    int32_t samples[128];
    size_t bytesRead;
    i2s_read(I2S_NUM_0, &samples, sizeof(samples), &bytesRead, 0);

    int32_t minSample = 2147483647;
    int32_t maxSample = -2147483647;
    for (int i = 0; i < (int)(bytesRead / 4); i++) {
        int32_t s = samples[i] >> 14;
        if (s < minSample) minSample = s;
        if (s > maxSample) maxSample = s;
    }
    long soundVolume = maxSample - minSample;

    // --- 3. SES TEPKİSİ ---
    if (soundVolume > 15000 && !isAngry) {
        // Cooldown kontrolü: 1 dakika bekleme süresi aktif mi?
        if (millis() < angryCooldownEnd) {
            // Bekleme süresinde, tetikleme
        } else {
            // İlk tetikleme zamanını kaydet
            if (angryCount == 0) firstAngryTime = millis();
            angryCount++;
            
            Serial.printf("[SES] KIZGIN! Seviye: %ld (Tetikleme #%d)\n", soundVolume, angryCount);
            angrySequence();
            
            // 3 kez tetiklendiyse 1 dakika bekle
            if (angryCount >= 3) {
                angryCooldownEnd = millis() + 60000; // 1 dakika
                angryCount = 0;
                Serial.println("[SES] 3 kez tetiklendi, 1 dakika beklemeye girdi.");
            }
            // 2 dakika içinde 3'e ulaşmadıysa sayacı sıfırla
            else if (millis() - firstAngryTime > 120000) {
                angryCount = 0;
            }
        }
    }

    // Debug: ses seviyesini göster (ayar yapmak için)
    static unsigned long lastSoundLog = 0;
    if (millis() - lastSoundLog > 3000) {
        Serial.printf("[SES] Seviye: %ld\n", soundVolume);
        lastSoundLog = millis();
    }

    // --- 4. YÜZ TAKİBİ (sadece sakinken) ---
    if (!isAngry) {
        if (faceDetected) {
            trackFace();
        } 
        else if (millis() - lastFaceTime > 5000 && lastFaceTime > 0) {
            // 5 saniyedir yüz yok → gözleri ÇOK YAVAS merkeze döndür
            // Anlık değil, her döngüde 1 piksel merkeze yaklaş
            if (targetLookX > 120) targetLookX--;
            else if (targetLookX < 120) targetLookX++;
            if (targetLookY > 120) targetLookY--;
            else if (targetLookY < 120) targetLookY++;
            
            // Motoru da yavaşça merkeze döndür
            returnMotorToCenter();
            
            // EMA filtresini de merkeze çek (bir sonraki yüz tespitinde sıçrama olmasın)
            smoothFaceX = smoothFaceX + 0.02 * (160.0 - smoothFaceX);
            smoothFaceY = smoothFaceY + 0.02 * (120.0 - smoothFaceY);
        }
    }

    // --- 5. RASTGELE KIRPMA ---
    if (random(0, 500) == 1) blinkNow = true;

    // --- 6. DEBUG (her 2 saniyede bir) ---
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 2000) {
        Serial.printf("[DEBUG] Yuz:%s X:%d Y:%d W:%d H:%d Motor:%d°\n",
            faceDetected ? "EVET" : "HAYIR",
            (int)faceX, (int)faceY, (int)faceW, (int)faceH,
            (motorPosition * 360) / stepsPerRevolution);
        lastDebug = millis();
    }

    delay(20);
}

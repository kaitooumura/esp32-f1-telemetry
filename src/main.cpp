#include <Arduino.h>
#include <WiFi.h>
#include <F1_25_UDP.h>   
#include <TFT_eSPI.h> 
#include <SPI.h>
#include <esp_task_wdt.h> 

const char* WIFI_SSID = "HKHC-2G";
const char* WIFI_PASS = "h14k04h09c12";
const int UDP_PORT = 20777; 

TFT_eSPI tft = TFT_eSPI();
F1_25_Parser parser;

// --- COORDENADAS FIXAS (480x320) ---
const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int CENTER_X = SCREEN_W / 2; // 240
const int CENTER_Y = SCREEN_H / 2; // 160

// Ajuste Fino das Posições Verticais
const int Y_SPEED = 50;   
const int Y_GEAR  = 160;  
const int Y_RPM   = 280;  

struct SharedTelemetryData {
    uint16_t speed;
    int8_t   gear;
    uint32_t currentLapTimeMS;
    uint32_t lastLapTimeMS;
    uint8_t  carPosition;
    uint8_t  currentLapNum;
    bool     drsAllowed;
    uint16_t rpm;
    float    ersStore;
    uint8_t  ersDeployMode;
    uint8_t  tyresAgeLaps;
    float    fuelRemainingLaps;
    uint8_t  tyreLifeSpan;
    uint16_t revLightsBitValue;  
    unsigned long lastRevLightsTimestamp;
};

SharedTelemetryData g_Telemetry = {0};
SemaphoreHandle_t g_TelemetryMutex;

void vTask_TelemetryUDP(void *pvParameters);
void vTask_DisplayTFT(void *pvParameters);

// --- Task de Rede (MANTIDA IGUAL) ---
void vTask_TelemetryUDP(void *pvParameters) {
    esp_task_wdt_add(NULL);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status()!= WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_task_wdt_reset();
    }
    parser.begin(UDP_PORT);

    while (1) {
        int packetsProcessed = 0;
        bool packetsRemaining = true;
        while (packetsRemaining && packetsProcessed < 20) {
            uint8_t packetId = parser.read();
            if (packetId == 255) {
                packetsRemaining = false;
            } 
            else if (packetId == 2 || packetId == 6 || packetId == 7 || packetId == 12) {
                if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    if (packetId == 2) { 
                        PacketLapData* p = parser.packetLapData();
                        uint8_t c = p->m_playerCarIndex();
                        g_Telemetry.currentLapTimeMS = p->m_lapData(c).m_currentLapTimeInMS;
                        g_Telemetry.lastLapTimeMS = p->m_lapData(c).m_lastLapTimeInMS;
                        g_Telemetry.carPosition = p->m_lapData(c).m_carPosition;
                        g_Telemetry.currentLapNum = p->m_lapData(c).m_currentLapNum;
                    } 
                    else if (packetId == 6) { 
                        PacketCarTelemetryData* p = parser.packetCarTelemetryData();
                        uint8_t c = p->m_playerCarIndex();
                        g_Telemetry.speed = p->m_carTelemetryData(c).m_speed;
                        g_Telemetry.gear = p->m_carTelemetryData(c).m_gear;
                        g_Telemetry.drsAllowed = (p->m_carTelemetryData(c).m_drs == 1);
                        g_Telemetry.rpm = p->m_carTelemetryData(c).m_engineRPM; 
                        g_Telemetry.revLightsBitValue = p->m_carTelemetryData(c).m_revLightsBitValue;
                        g_Telemetry.lastRevLightsTimestamp = millis();
                    } 
                    else if (packetId == 7) {
                        PacketCarStatusData* p = parser.packetCarStatusData();
                        uint8_t c = p->m_playerCarIndex();
                        g_Telemetry.ersStore = p->m_carStatusData(c).m_ersStoreEnergy;
                        g_Telemetry.ersDeployMode = p->m_carStatusData(c).m_ersDeployMode;
                        g_Telemetry.tyresAgeLaps = p->m_carStatusData(c).m_tyresAgeLaps;
                        g_Telemetry.fuelRemainingLaps = p->m_carStatusData(c).m_fuelRemainingLaps;
                    } 
                    else if (packetId == 12) {
                        PacketTyreSetData* p = parser.packetTyreSetData();
                        uint8_t c = p->m_playerCarIndex();
                        g_Telemetry.tyreLifeSpan = p->m_tyresetData(c).m_lifeSpan;
                    }
                    xSemaphoreGive(g_TelemetryMutex);
                }
            }
            packetsProcessed++;
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1));
    } 
}

// --- Task de Display (CORREÇÕES VISUAIS V28) ---
void vTask_DisplayTFT(void *pvParameters) {
    esp_task_wdt_add(NULL);

    SharedTelemetryData localTelemetry = {0};
    char buf[32]; 
    const float MAX_ERS_JOULES = 4000000.0f; 

    #define C_BG TFT_BLACK
    #define C_TXT TFT_WHITE
    #define C_ACCENT TFT_CYAN
    #define C_WARN TFT_RED
    #define C_GOOD TFT_GREEN

    tft.setTextDatum(MC_DATUM); 
    bool wasShiftMode = false;

    while (1) {
        if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&localTelemetry, &g_Telemetry, sizeof(SharedTelemetryData));
            xSemaphoreGive(g_TelemetryMutex);
        } 
        esp_task_wdt_reset();

        // ---------------------------------------------------------
        // 1. MARCHA (Centro)
        // ---------------------------------------------------------
        unsigned long age = millis() - localTelemetry.lastRevLightsTimestamp;
        bool shiftNow = (age < 100) && (localTelemetry.revLightsBitValue >= 2047) && (localTelemetry.rpm > 10000);
        
        uint16_t gearBg = shiftNow ? C_WARN : C_BG; 
        uint16_t gearFg = shiftNow ? C_BG : C_TXT;  

        if (shiftNow != wasShiftMode) {
            // Caixa de limpeza otimizada
            tft.fillRect(CENTER_X - 40, Y_GEAR - 45, 80, 90, gearBg);
            wasShiftMode = shiftNow;
        }

        tft.setTextColor(gearFg, gearBg);
        
        // Lógica de Tamanho para corrigir N/R sumindo
        if (localTelemetry.gear == -1) {
            tft.setTextSize(3); // Ré Grande
            tft.drawString("R", CENTER_X, Y_GEAR, 4); 
        } 
        else if (localTelemetry.gear == 0) {
            tft.setTextSize(3); // Neutro Grande
            tft.drawString("N", CENTER_X, Y_GEAR, 4);
        } 
        else {
            sprintf(buf, "%d", localTelemetry.gear);
            tft.setTextSize(1); // Normal (A Fonte 8 já é gigante)
            int padding = tft.textWidth("8", 8);
            tft.setTextPadding(padding); 
            tft.drawString(buf, CENTER_X, Y_GEAR, 8); 
            tft.setTextPadding(0);
        }

        esp_task_wdt_reset();

        // ---------------------------------------------------------
        // 2. VELOCIDADE (Acima) - CORRIGIDO
        // ---------------------------------------------------------
        tft.setTextColor(C_ACCENT, C_BG); 
        // CORREÇÃO: Voltei para Size 1. Fonte 7 já é grande (Digital).
        // Na V27 estava Size 2, por isso ficou gigante.
        tft.setTextSize(1); 
        sprintf(buf, "%03d", localTelemetry.speed);
        tft.drawString(buf, CENTER_X, Y_SPEED, 7); 

        // ---------------------------------------------------------
        // 3. RPM (Abaixo) - CORRIGIDO
        // ---------------------------------------------------------
        tft.setTextColor(C_TXT, C_BG);
        // CORREÇÃO: Aumentei para Size 2 (Fonte 4).
        // Isso vai deixar o RPM mais legível no rodapé.
        tft.setTextSize(2); 
        sprintf(buf, "%d", localTelemetry.rpm);
        tft.drawString(buf, CENTER_X, Y_RPM, 4); 

        esp_task_wdt_reset();

        // ---------------------------------------------------------
        // 4. ERS (Esquerda)
        // ---------------------------------------------------------
        int barX = 15;
        int barY = 60;
        int barW = 25;
        int barH = 200;
        
        tft.drawRect(barX, barY, barW, barH, C_TXT);
        
        float ersPct = localTelemetry.ersStore / MAX_ERS_JOULES;
        if(ersPct > 1.0) ersPct = 1.0;
        int h_fill = (int)(ersPct * (barH - 4));
        
        tft.fillRect(barX + 2, (barY + barH - 2) - h_fill, barW - 4, h_fill, (ersPct < 0.2) ? C_WARN : C_ACCENT);
        tft.fillRect(barX + 2, barY + 2, barW - 4, (barH - 4) - h_fill, C_BG);

        tft.setTextColor(C_TXT, C_BG);
        tft.setTextSize(1); // Reset para tamanho normal
        const char* ersModeStr = "N/A"; 
        switch (localTelemetry.ersDeployMode) {
            case 1: ersModeStr = "MED"; break;
            case 2: ersModeStr = "HOT"; break;
            case 3: ersModeStr = "OVT"; break;
        }
        tft.drawString(ersModeStr, barX + 12, barY + barH + 20, 4);

        // ---------------------------------------------------------
        // 5. DADOS LATERAIS (Direita) - ESPAÇAMENTO AJUSTADO
        // ---------------------------------------------------------
        tft.setTextDatum(TR_DATUM); 
        tft.setTextColor(C_TXT, C_BG);
        tft.setTextSize(1); 
        
        int rightX = 470; 
        int startY = 60;
        // Aumentei o espaçamento de 40 para 50px para espalhar melhor
        int stepY = 50; 

        sprintf(buf, "Age: %d", localTelemetry.tyresAgeLaps);
        tft.drawString(buf, rightX, startY, 4);

        sprintf(buf, "Left: %d", localTelemetry.tyreLifeSpan);
        tft.drawString(buf, rightX, startY + stepY, 4);

        sprintf(buf, "Fuel: %.1f", localTelemetry.fuelRemainingLaps);
        tft.drawString(buf, rightX, startY + (stepY * 2), 4);

        tft.setTextColor(C_GOOD, C_BG);
        uint32_t t = localTelemetry.lastLapTimeMS;
        int min = t / 60000;
        int sec = (t % 60000) / 1000;
        int ms = t % 1000;
        sprintf(buf, "%d:%02d.%03d", min, sec, ms);
        tft.drawString(buf, rightX, startY + (stepY * 3), 4);

        tft.setTextDatum(MC_DATUM); 

        // ---------------------------------------------------------
        // 6. DRS (Inferior Direito)
        // ---------------------------------------------------------
        if (localTelemetry.drsAllowed) {
            tft.fillRoundRect(380, 270, 80, 40, 5, C_GOOD);
            tft.setTextColor(C_BG, C_GOOD); 
            tft.setTextSize(1);
            tft.drawString("DRS", 420, 290, 4);
        } else {
            tft.fillRoundRect(380, 270, 80, 40, 5, C_BG);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10)); 
    } 
}

void setup() {
    Serial.begin(115200); 
    
    esp_task_wdt_init(30, true); 

    tft.init();
    tft.setRotation(1); 
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Iniciando V28...", CENTER_X, CENTER_Y, 4);
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        esp_task_wdt_reset();
    }

    tft.fillScreen(TFT_BLACK);
    tft.drawString("WiFi OK!", CENTER_X, CENTER_Y - 20, 4);
    tft.drawString(WiFi.localIP().toString(), CENTER_X, CENTER_Y + 20, 4);
    
    delay(3000);
    tft.fillScreen(TFT_BLACK);

    g_TelemetryMutex = xSemaphoreCreateMutex(); 
    xTaskCreatePinnedToCore(vTask_TelemetryUDP, "Task_UDP", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(vTask_DisplayTFT, "Task_TFT_V28", 8192, NULL, 1, NULL, 0); 
}

void loop() { 
    vTaskDelay(pdMS_TO_TICKS(5000)); 
}
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

const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int CENTER_X = SCREEN_W / 2;
const int CENTER_Y = SCREEN_H / 2;

const int Y_SPEED = 60;   
const int Y_GEAR  = 170;  
const int Y_RPM   = 290;  

struct SharedTelemetryData {
    // Básicos
    uint16_t speed;
    int8_t   gear;
    uint16_t rpm;
    uint16_t revLightsBitValue;  
    unsigned long lastRevLightsTimestamp;

    // Tempos
    uint32_t currentLapTimeMS;
    uint32_t lastLapTimeMS;
    uint8_t  carPosition;
    uint8_t  currentLapNum;
    uint8_t  totalLaps;
    
    // Carro
    float    ersStore;
    uint8_t  ersDeployMode;
    float    fuelRemainingLaps;
    uint8_t  fuelMix;
    
    bool     drsAllowed;
    bool     drsActive;

    uint8_t  tyreLifeSpan;
    uint8_t  tyresAgeLaps;
    float    tyresWear[4];
    uint8_t  tyresTemp[4];

    // --- SETORES (ATUAIS) ---
    uint16_t currentS1, currentS2; 
    uint32_t historyS3;            
    
    // --- RECORDES (PESSOAL + SESSÃO) ---
    uint32_t bestS1, bestS2, bestS3; 
    uint32_t sessionBestS1, sessionBestS2, sessionBestS3;
    
    uint8_t  safetyCarStatus;
    float    safetyCarDelta;
};

SharedTelemetryData g_Telemetry = {0};
SemaphoreHandle_t g_TelemetryMutex;

void vTask_TelemetryUDP(void *pvParameters);
void vTask_DisplayTFT(void *pvParameters);

// --- Task de Rede ---
void vTask_TelemetryUDP(void *pvParameters) {
    esp_task_wdt_add(NULL);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status()!= WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_task_wdt_reset();
    }
    parser.begin(UDP_PORT);

    // Inicializa recordes com valor alto para poder baixar
    g_Telemetry.sessionBestS1 = 0xFFFFFFFF;
    g_Telemetry.sessionBestS2 = 0xFFFFFFFF;
    g_Telemetry.sessionBestS3 = 0xFFFFFFFF;
    g_Telemetry.bestS1 = 0xFFFFFFFF;
    g_Telemetry.bestS2 = 0xFFFFFFFF;
    g_Telemetry.bestS3 = 0xFFFFFFFF;

    while (1) {
        int packetsProcessed = 0;
        bool packetsRemaining = true;
        while (packetsRemaining && packetsProcessed < 20) {
            uint8_t packetId = parser.read();
            if (packetId == 255) {
                packetsRemaining = false;
            } 
            else if (packetId == 1 || packetId == 2 || packetId == 6 || packetId == 7 || packetId == 10 || packetId == 11 || packetId == 12) {
                if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    
                    if (packetId == 1) { 
                        PacketSessionData* p = parser.packetSessionData();
                        g_Telemetry.totalLaps = p->m_totalLaps();
                        g_Telemetry.safetyCarStatus = p->m_safetyCarStatus();
                    }
                    else if (packetId == 2) { 
                        PacketLapData* p = parser.packetLapData();
                        uint8_t c = p->m_playerCarIndex();
                        
                        g_Telemetry.currentLapTimeMS = p->m_lapData(c).m_currentLapTimeInMS;
                        g_Telemetry.lastLapTimeMS = p->m_lapData(c).m_lastLapTimeInMS;
                        g_Telemetry.carPosition = p->m_lapData(c).m_carPosition;
                        g_Telemetry.currentLapNum = p->m_lapData(c).m_currentLapNum;
                        g_Telemetry.safetyCarDelta = p->m_lapData(c).m_safetyCarDelta;
                        g_Telemetry.currentS1 = p->m_lapData(c).m_sector1TimeInMSPart;
                        g_Telemetry.currentS2 = p->m_lapData(c).m_sector2TimeInMSPart;
                    } 
                    else if (packetId == 6) { 
                        PacketCarTelemetryData* p = parser.packetCarTelemetryData();
                        uint8_t c = p->m_playerCarIndex();
                        g_Telemetry.speed = p->m_carTelemetryData(c).m_speed;
                        g_Telemetry.gear = p->m_carTelemetryData(c).m_gear;
                        g_Telemetry.drsActive = (p->m_carTelemetryData(c).m_drs == 1);
                        g_Telemetry.rpm = p->m_carTelemetryData(c).m_engineRPM; 
                        g_Telemetry.revLightsBitValue = p->m_carTelemetryData(c).m_revLightsBitValue;
                        g_Telemetry.lastRevLightsTimestamp = millis();
                        for(int i=0; i<4; i++) g_Telemetry.tyresTemp[i] = p->m_carTelemetryData(c).m_tyresSurfaceTemperature[i];
                    } 
                    else if (packetId == 7) { 
                        PacketCarStatusData* p = parser.packetCarStatusData();
                        uint8_t c = p->m_playerCarIndex();
                        g_Telemetry.ersStore = p->m_carStatusData(c).m_ersStoreEnergy;
                        g_Telemetry.ersDeployMode = p->m_carStatusData(c).m_ersDeployMode;
                        g_Telemetry.tyresAgeLaps = p->m_carStatusData(c).m_tyresAgeLaps;
                        g_Telemetry.fuelRemainingLaps = p->m_carStatusData(c).m_fuelRemainingLaps;
                        g_Telemetry.fuelMix = p->m_carStatusData(c).m_fuelMix;
                        g_Telemetry.drsAllowed = p->m_carStatusData(c).m_drsAllowed;
                    } 
                    else if (packetId == 10) { 
                        PacketCarDamageData* p = parser.packetCarDamageData();
                        uint8_t c = parser.packetCarStatusData()->m_playerCarIndex();
                        for(int i=0; i<4; i++) g_Telemetry.tyresWear[i] = p->m_carDamageData(c).m_tyresWear[i];
                    }
                    else if (packetId == 11) { 
                        // --- HISTÓRICO DE SESSÃO (PESSOAL E GLOBAL) ---
                        PacketSessionHistoryData* p = parser.packetSessionHistoryData();
                        
                        // 1. Extrai os melhores tempos deste carro (seja qual for)
                        uint32_t thisCarBestS1 = 0xFFFFFFFF;
                        uint32_t thisCarBestS2 = 0xFFFFFFFF;
                        uint32_t thisCarBestS3 = 0xFFFFFFFF;

                        uint8_t lapS1 = p->m_bestSector1LapNum();
                        if (lapS1 > 0) {
                            LapHistoryData h = p->m_lapHistoryData(lapS1 - 1);
                            thisCarBestS1 = (h.m_sector1TimeMinutes * 60000) + h.m_sector1TimeInMS;
                        }
                        uint8_t lapS2 = p->m_bestSector2LapNum();
                        if (lapS2 > 0) {
                            LapHistoryData h = p->m_lapHistoryData(lapS2 - 1);
                            thisCarBestS2 = (h.m_sector2TimeMinutes * 60000) + h.m_sector2TimeInMS;
                        }
                        uint8_t lapS3 = p->m_bestSector3LapNum();
                        if (lapS3 > 0) {
                            LapHistoryData h = p->m_lapHistoryData(lapS3 - 1);
                            thisCarBestS3 = (h.m_sector3TimeMinutes * 60000) + h.m_sector3TimeInMS;
                        }

                        // 2. Atualiza o MELHOR DA SESSÃO (Roxo)
                        if (thisCarBestS1 < g_Telemetry.sessionBestS1) g_Telemetry.sessionBestS1 = thisCarBestS1;
                        if (thisCarBestS2 < g_Telemetry.sessionBestS2) g_Telemetry.sessionBestS2 = thisCarBestS2;
                        if (thisCarBestS3 < g_Telemetry.sessionBestS3) g_Telemetry.sessionBestS3 = thisCarBestS3;

                        // 3. Se for o MEU carro, atualiza meus recordes (Verde) e pega o S3 da ultima volta
                        if (p->m_carIdx() == parser.packetCarStatusData()->m_playerCarIndex()) {
                            g_Telemetry.bestS1 = thisCarBestS1;
                            g_Telemetry.bestS2 = thisCarBestS2;
                            g_Telemetry.bestS3 = thisCarBestS3;

                            // Pega o S3 da última volta completada para exibir
                            uint8_t currentLap = g_Telemetry.currentLapNum;
                            if (currentLap > 1) {
                                int lastLapIndex = currentLap - 2; 
                                if (lastLapIndex >= 0 && lastLapIndex < 100) {
                                    LapHistoryData lastLap = p->m_lapHistoryData(lastLapIndex);
                                    if (lastLap.m_lapValidBitFlags & 0x08) { 
                                        g_Telemetry.historyS3 = (lastLap.m_sector3TimeMinutes * 60000) + lastLap.m_sector3TimeInMS;
                                    }
                                }
                            }
                        }
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

// --- Task de Display (V40 - Cores Corretas) ---
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
    #define C_PURPLE TFT_MAGENTA // Roxo
    #define C_ORANGE TFT_ORANGE
    #define C_BLUE TFT_BLUE

    tft.setTextDatum(MC_DATUM); 
    bool wasShiftMode = false;
    bool blinkState = false;
    unsigned long lastBlink = 0;
    
    uint32_t displayS1 = 0;
    uint32_t displayS2 = 0;

    while (1) {
        if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&localTelemetry, &g_Telemetry, sizeof(SharedTelemetryData));
            xSemaphoreGive(g_TelemetryMutex);
        } 
        esp_task_wdt_reset();

        // --- SAFETY CAR ---
        if (localTelemetry.safetyCarStatus != 0) {
            uint16_t scBgColor = C_GOOD; 
            if (localTelemetry.safetyCarDelta < 0) { 
                if (millis() - lastBlink > 200) { 
                    blinkState = !blinkState;
                    lastBlink = millis();
                }
                scBgColor = blinkState ? TFT_YELLOW : TFT_BLACK; 
            }
            tft.fillScreen(scBgColor);
            tft.setTextColor(TFT_BLACK, scBgColor); 
            tft.setTextSize(3);
            if (localTelemetry.safetyCarStatus == 1) tft.drawString("SAFETY CAR", CENTER_X, 50, 4);
            else tft.drawString("VIRTUAL SC", CENTER_X, 50, 4);
            tft.setTextSize(4);
            sprintf(buf, "%+.2f", localTelemetry.safetyCarDelta); 
            tft.drawString(buf, CENTER_X, 200, 7);
            vTaskDelay(pdMS_TO_TICKS(33)); 
            continue; 
        }
        
        // 1. CABEÇALHO
        tft.setTextColor(C_TXT, C_BG);
        tft.setTextSize(1);
        tft.setTextDatum(TL_DATUM); 
        tft.setTextPadding(100); 
        sprintf(buf, "POS: %d", localTelemetry.carPosition);
        tft.drawString(buf, 10, 10, 4);
        tft.setTextPadding(0);

        tft.setTextDatum(TR_DATUM); 
        sprintf(buf, "LAP: %d/%d", localTelemetry.currentLapNum, localTelemetry.totalLaps);
        tft.drawString(buf, 470, 10, 4);
        tft.setTextDatum(MC_DATUM); 

        // 2. MARCHA
        unsigned long age = millis() - localTelemetry.lastRevLightsTimestamp;
        bool shiftNow = (age < 100) && (localTelemetry.revLightsBitValue >= 2047) && (localTelemetry.rpm > 11500);
        
        uint16_t gearBg = shiftNow ? C_WARN : C_BG; 
        uint16_t gearFg = shiftNow ? C_BG : C_TXT;  

        if (shiftNow != wasShiftMode) {
            tft.fillRect(CENTER_X - 40, Y_GEAR - 45, 80, 90, gearBg);
            wasShiftMode = shiftNow;
        }

        tft.setTextColor(gearFg, gearBg);
        if (localTelemetry.gear == -1) { tft.setTextSize(3); tft.drawString("R", CENTER_X, Y_GEAR, 4); } 
        else if (localTelemetry.gear == 0) { tft.setTextSize(3); tft.drawString("N", CENTER_X, Y_GEAR, 4); } 
        else {
            sprintf(buf, "%d", localTelemetry.gear);
            tft.setTextSize(1); 
            tft.setTextPadding(tft.textWidth("88", 8)); 
            tft.drawString(buf, CENTER_X, Y_GEAR, 8); 
            tft.setTextPadding(0);
        }
        esp_task_wdt_reset();

        // 3. VELOCIDADE & RPM
        tft.setTextColor(C_ACCENT, C_BG); 
        tft.setTextSize(1); 
        sprintf(buf, "%03d", localTelemetry.speed);
        tft.setTextPadding(tft.textWidth("888", 7));
        tft.drawString(buf, CENTER_X, Y_SPEED, 7); 
        tft.setTextPadding(0);

        tft.setTextColor(C_TXT, C_BG);
        tft.setTextSize(2); 
        sprintf(buf, "%d", localTelemetry.rpm);
        tft.setTextPadding(tft.textWidth("88888", 4));
        tft.drawString(buf, CENTER_X, Y_RPM, 4); 
        tft.setTextPadding(0);

        // 4. ERS & COMBUSTÍVEL
        int barX = 15; int barY = 60; int barW = 25; int barH = 200;
        tft.drawRect(barX, barY, barW, barH, C_TXT);
        float ersPct = localTelemetry.ersStore / MAX_ERS_JOULES;
        if(ersPct > 1.0) ersPct = 1.0;
        int h_fill = (int)(ersPct * (barH - 4));
        tft.fillRect(barX + 2, (barY + barH - 2) - h_fill, barW - 4, h_fill, (ersPct < 0.2) ? C_WARN : C_ACCENT);
        tft.fillRect(barX + 2, barY + 2, barW - 4, (barH - 4) - h_fill, C_BG);

        tft.setTextColor(C_TXT, C_BG);
        tft.setTextSize(1);
        const char* ersModeStr = "N/A"; 
        switch (localTelemetry.ersDeployMode) {
            case 1: ersModeStr = "MED"; break;
            case 2: ersModeStr = "HOT"; break;
            case 3: ersModeStr = "OVT"; break;
        }
        tft.setTextPadding(60);
        tft.drawString(ersModeStr, barX + 12, barY + barH + 20, 4);
        
        const char* fuelMixStr = "FM:2";
        if (localTelemetry.fuelMix == 0) fuelMixStr = "FM:1";
        if (localTelemetry.fuelMix == 1) fuelMixStr = "FM:2";
        if (localTelemetry.fuelMix == 2) fuelMixStr = "FM:3";
        if (localTelemetry.fuelMix == 3) fuelMixStr = "FM:4";
        tft.drawString(fuelMixStr, barX + 18, barY + barH + 45, 4);
        tft.setTextPadding(0);

        // 5. PNEUS
        int tyreBoxX = 80; int tyreBoxY = 100;
        int tyreW = 25; int tyreH = 40; int gap = 5;
        int mapIdx[4] = {2, 3, 0, 1}; 
        int xOffsets[4] = {0, tyreW + gap, 0, tyreW + gap};
        int yOffsets[4] = {0, 0, tyreH + gap, tyreH + gap};

        for(int i=0; i<4; i++) {
            int idx = mapIdx[i];
            int x = tyreBoxX + xOffsets[i];
            int y = tyreBoxY + yOffsets[i];
            uint16_t tColor = C_GOOD;
            if (localTelemetry.tyresTemp[idx] < 80) tColor = C_BLUE;
            else if (localTelemetry.tyresTemp[idx] > 100) tColor = C_WARN;
            tft.drawRect(x, y, tyreW, tyreH, tColor);
            float wear = localTelemetry.tyresWear[idx];
            float life = 100.0 - wear; 
            if (life < 0) life = 0;
            int lifeH = (int)((life / 100.0) * (tyreH - 2));
            tft.fillRect(x+1, (y+tyreH-1) - lifeH, tyreW-2, lifeH, tColor);
            tft.fillRect(x+1, y+1, tyreW-2, (tyreH-2) - lifeH, C_BG);
        }
        esp_task_wdt_reset();

        // 6. LATERAL DIREITA
        tft.setTextDatum(TR_DATUM); 
        tft.setTextColor(C_TXT, C_BG);
        tft.setTextSize(1); 
        int rightX = 470; int startY = 60; int stepY = 35;
        sprintf(buf, "Age: %d", localTelemetry.tyresAgeLaps);
        tft.setTextPadding(120);
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

        // --- FUNÇÃO DE COR COM ROXO ---
        auto getSectorColor = [&](uint32_t time, uint32_t pBest, uint32_t sBest) {
            if (time == 0) return C_TXT;
            if (sBest > 0 && time <= sBest) return C_PURPLE; // Roxo (Session)
            if (pBest > 0 && time <= pBest) return C_GOOD;   // Verde (Pessoal)
            return C_WARN; // Vermelho
        };

        // S1
        if (localTelemetry.currentS1 > 0) displayS1 = localTelemetry.currentS1;
        tft.setTextColor(getSectorColor(displayS1, localTelemetry.bestS1, localTelemetry.sessionBestS1), C_BG);
        sprintf(buf, "S1: %d.%03d", displayS1 / 1000, displayS1 % 1000);
        if (displayS1 == 0) sprintf(buf, "S1: --.---");
        tft.drawString(buf, rightX, startY + (stepY * 4), 4);

        // S2
        if (localTelemetry.currentS2 > 0) displayS2 = localTelemetry.currentS2;
        tft.setTextColor(getSectorColor(displayS2, localTelemetry.bestS2, localTelemetry.sessionBestS2), C_BG);
        sprintf(buf, "S2: %d.%03d", displayS2 / 1000, displayS2 % 1000);
        if (displayS2 == 0) sprintf(buf, "S2: --.---");
        tft.drawString(buf, rightX, startY + (stepY * 5), 4);

        // S3 (Corrigido com cores)
        uint32_t s3 = localTelemetry.historyS3;
        tft.setTextColor(getSectorColor(s3, localTelemetry.bestS3, localTelemetry.sessionBestS3), C_BG);
        sprintf(buf, "S3: %d.%03d", s3 / 1000, s3 % 1000);
        if (s3 == 0) sprintf(buf, "S3: --.---");
        tft.drawString(buf, rightX, startY + (stepY * 6), 4);
        
        tft.setTextPadding(0);

        // 7. DRS
        tft.setTextDatum(MC_DATUM); 
        int drsBoxX = 75; int drsBoxY = 270; int drsW = 80; int drsH = 40;
        if (localTelemetry.drsActive) {
            tft.fillRoundRect(drsBoxX, drsBoxY, drsW, drsH, 5, C_GOOD);
            tft.setTextColor(C_BG, C_GOOD); tft.setTextSize(1);
            tft.drawString("DRS", drsBoxX + 40, drsBoxY + 20, 4);
        } 
        else if (localTelemetry.drsAllowed) {
            tft.drawRoundRect(drsBoxX, drsBoxY, drsW, drsH, 5, C_ORANGE);
            tft.fillRoundRect(drsBoxX+1, drsBoxY+1, drsW-2, drsH-2, 5, C_BG); 
            tft.setTextColor(C_ORANGE, C_BG); tft.setTextSize(1);
            tft.drawString("DRS", drsBoxX + 40, drsBoxY + 20, 4);
        } 
        else {
            tft.fillRoundRect(drsBoxX, drsBoxY, drsW, drsH, 5, C_BG);
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
    tft.drawString("Iniciando V40...", CENTER_X, CENTER_Y, 4);
    
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
    xTaskCreatePinnedToCore(vTask_DisplayTFT, "Task_TFT_V40", 8192, NULL, 1, NULL, 0); 
}

void loop() { 
    vTaskDelay(pdMS_TO_TICKS(5000)); 
}
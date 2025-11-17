// --- INÍCIO de src/main.cpp (VISUAL LEGÍVEL + REDE RÁPIDA) ---

#include <Arduino.h>
#include <WiFi.h>
#include <U8g2lib.h>     // Biblioteca do display OLED
#include <F1_25_UDP.h>   // A biblioteca de parsing

// --- Configurações de Rede ---
const char* WIFI_SSID = "HKHC-2G";
const char* WIFI_PASS = "h14k04h09c12";
const int UDP_PORT = 20777;

// --- Configurações do Display OLED (Hardware SPI) ---
#define PIN_OLED_SCK 18  
#define PIN_OLED_MOSI 23 
#define PIN_OLED_CS 5
#define PIN_OLED_DC 16
#define PIN_OLED_RES 17

// Construtor do OLED
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(
    U8G2_R0, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RES
);

// --- Objeto Parser ---
F1_25_Parser parser;

// --- Estrutura de Dados ---
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

// --- Protótipos ---
void vTask_TelemetryUDP(void *pvParameters);
void vTask_DisplayOLED(void *pvParameters);

// --- Task de Rede (MODO "LIMPA FILA" - SEM LAG) ---
void vTask_TelemetryUDP(void *pvParameters) {
    Serial.println("Task UDP: Iniciando...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status()!= WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println("WiFi Conectado!");
    Serial.println(WiFi.localIP()); 

    parser.begin(UDP_PORT);

    while (1) {
        // Lógica para limpar todo o buffer de rede antes de dormir
        bool packetsRemaining = true;
        
        while (packetsRemaining) {
            uint8_t packetId = parser.read();

            if (packetId == 255) {
                packetsRemaining = false; // Fila limpa
            } 
            else if (packetId == 2 || packetId == 6 || packetId == 7 || packetId == 12) {
                
                if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    
                    if (packetId == 2) { 
                        PacketLapData* p = parser.packetLapData();
                        uint8_t carIndex = p->m_playerCarIndex();
                        g_Telemetry.currentLapTimeMS = p->m_lapData(carIndex).m_currentLapTimeInMS;
                        g_Telemetry.lastLapTimeMS = p->m_lapData(carIndex).m_lastLapTimeInMS;
                        g_Telemetry.carPosition = p->m_lapData(carIndex).m_carPosition;
                        g_Telemetry.currentLapNum = p->m_lapData(carIndex).m_currentLapNum;
                    } 
                    else if (packetId == 6) { 
                        PacketCarTelemetryData* p = parser.packetCarTelemetryData();
                        uint8_t carIndex = p->m_playerCarIndex();
                        g_Telemetry.speed = p->m_carTelemetryData(carIndex).m_speed;
                        g_Telemetry.gear = p->m_carTelemetryData(carIndex).m_gear;
                        g_Telemetry.drsAllowed = (p->m_carTelemetryData(carIndex).m_drs == 1);
                        g_Telemetry.rpm = p->m_carTelemetryData(carIndex).m_engineRPM; 
                        g_Telemetry.revLightsBitValue = p->m_carTelemetryData(carIndex).m_revLightsBitValue;
                        g_Telemetry.lastRevLightsTimestamp = millis(); 
                    } 
                    else if (packetId == 7) {
                        PacketCarStatusData* p = parser.packetCarStatusData();
                        uint8_t carIndex = p->m_playerCarIndex();
                        g_Telemetry.ersStore = p->m_carStatusData(carIndex).m_ersStoreEnergy;
                        g_Telemetry.ersDeployMode = p->m_carStatusData(carIndex).m_ersDeployMode;
                        g_Telemetry.tyresAgeLaps = p->m_carStatusData(carIndex).m_tyresAgeLaps;
                        g_Telemetry.fuelRemainingLaps = p->m_carStatusData(carIndex).m_fuelRemainingLaps;
                    } 
                    else if (packetId == 12) {
                        PacketTyreSetData* p = parser.packetTyreSetData();
                        uint8_t carIndex = p->m_playerCarIndex();
                        g_Telemetry.tyreLifeSpan = p->m_tyresetData(carIndex).m_lifeSpan;
                    }
                    
                    xSemaphoreGive(g_TelemetryMutex);
                }
            }
        }
        // Só dorme depois de ler tudo
        vTaskDelay(pdMS_TO_TICKS(1));
    } 
}

// --- Task de Display (VISUAL ANTIGO E LEGÍVEL) ---
void vTask_DisplayOLED(void *pvParameters) {
    Serial.println("Task OLED: Iniciando...");

    SharedTelemetryData localTelemetry = {0};
    char format_buf[64]; 
    const float MAX_ERS_JOULES = 4000000.0f; 

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(33); // 30 FPS é suficiente para o OLED
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&localTelemetry, &g_Telemetry, sizeof(SharedTelemetryData));
            xSemaphoreGive(g_TelemetryMutex);
        } 

        u8g2.clearBuffer();
        
        // 1. Tempo da Última Volta (Topo Esq)
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        uint32_t t = localTelemetry.lastLapTimeMS;
        int min = t / 60000;
        int sec = (t % 60000) / 1000;
        int ms = t % 1000;
        sprintf(format_buf, "%d:%02d.%03d", min, sec, ms);
        u8g2.drawStr(0, 10, format_buf);
        
        // 2. Posição e Volta (Topo Dir)
        sprintf(format_buf, "P%d V%d", localTelemetry.carPosition, localTelemetry.currentLapNum);
        int pos_lap_width = u8g2.getStrWidth(format_buf);
        u8g2.drawStr(128 - pos_lap_width - 2, 10, format_buf);

        // 3. Velocidade (Centro, Topo)
        u8g2.setFont(u8g2_font_ncenB12_tr); 
        sprintf(format_buf, "%03d", localTelemetry.speed);
        int speed_width = u8g2.getStrWidth(format_buf);
        u8g2.drawStr((128 - speed_width) / 2, 24, format_buf);

        // 4. Marcha (Centro, Grande)
        u8g2.setFont(u8g2_font_logisoso28_tr); 
        if (localTelemetry.gear == -1) sprintf(format_buf, "R");
        else if (localTelemetry.gear == 0) sprintf(format_buf, "N");
        else sprintf(format_buf, "%d", localTelemetry.gear);
        
        int gear_width = u8g2.getStrWidth(format_buf);
        int gear_x = (128 - gear_width) / 2;

        // Lógica de Inversão da Marcha (Corrected)
        unsigned long age = millis() - localTelemetry.lastRevLightsTimestamp;
        bool showRevLight = (age < 100) && (localTelemetry.revLightsBitValue >= 2047) && (localTelemetry.rpm > 10000);

        if (showRevLight) {
            u8g2.drawBox(gear_x - 2, 30, gear_width + 4, 28); 
            u8g2.setFontMode(0); 
            u8g2.setDrawColor(0); 
            u8g2.drawStr(gear_x, 56, format_buf);
            u8g2.setDrawColor(1); 
        } else {
            u8g2.drawStr(gear_x, 56, format_buf);
        }

        // 5. RPM (Abaixo da Marcha)
        u8g2.setFont(u8g2_font_ncenB08_tr);
        sprintf(format_buf, "%d", localTelemetry.rpm); 
        int rpm_width = u8g2.getStrWidth(format_buf);
        u8g2.drawStr((128 - rpm_width) / 2, 64, format_buf);

        // 6. Barra de ERS (Esquerda)
        u8g2.drawFrame(5, 12, 8, 42); 
        float ersPercent = localTelemetry.ersStore / MAX_ERS_JOULES;
        if (ersPercent > 1.0f) ersPercent = 1.0f;
        int barHeight = (int)(ersPercent * 40); 
        if (barHeight > 0) u8g2.drawBox(6, (12 + 41) - barHeight, 6, barHeight);
        
        // 7. Modo de ERS (Abaixo da barra)
        u8g2.setFont(u8g2_font_ncenB08_tr);
        const char* ersModeStr = "N/A"; 
        switch (localTelemetry.ersDeployMode) {
            case 1: ersModeStr = "MED"; break;
            case 2: ersModeStr = "HL"; break;
            case 3: ersModeStr = "OVT"; break;
        }
        u8g2.drawStr(4, 64, ersModeStr);

        // 8. Dados Laterais (FONTE GRANDE LEGÍVEL - RESTAURADO)
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        
        sprintf(format_buf, "t_age:%d", localTelemetry.tyresAgeLaps);
        int w1 = u8g2.getStrWidth(format_buf);
        u8g2.drawStr(128 - w1 - 2, 26, format_buf);

        sprintf(format_buf, "t_left:%d", localTelemetry.tyreLifeSpan);
        int w2 = u8g2.getStrWidth(format_buf);
        u8g2.drawStr(128 - w2 - 2, 38, format_buf);

        sprintf(format_buf, "fuel:%.1f", localTelemetry.fuelRemainingLaps);
        int w3 = u8g2.getStrWidth(format_buf);
        u8g2.drawStr(128 - w3 - 2, 50, format_buf);

        // 9. DRS (Canto Inferior Direito - Posição Ajustada)
        if (localTelemetry.drsAllowed) {
            u8g2.setFont(u8g2_font_ncenB10_tr); 
            u8g2.drawBox(94, 52, 30, 12);
            u8g2.setFontMode(0);
            u8g2.setDrawColor(0);
            u8g2.drawStr(96, 62, "DRS");
            u8g2.setDrawColor(1);
        }

        u8g2.sendBuffer();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    } 
}

void setup() {
    Serial.begin(115200); 
    
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Iniciando...");
    u8g2.sendBuffer();

    g_TelemetryMutex = xSemaphoreCreateMutex(); 

    xTaskCreatePinnedToCore(vTask_TelemetryUDP, "Task_UDP", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(vTask_DisplayOLED, "Task_OLED", 4096, NULL, 1, NULL, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000)); 
}
// --- INÍCIO de src/main.cpp (V16 - TFT_eSPI ST7796S) ---

#include <Arduino.h>
#include <WiFi.h>
#include <F1_25_UDP.h>   // A biblioteca de parsing de telemetria!

// --- NOVAS BIBLIOTECAS DE DISPLAY ---
#include <TFT_eSPI.h>
#include <SPI.h>

// --- Objeto do Novo Display ---
TFT_eSPI tft = TFT_eSPI();

// Posições X centrais (calculadas para 320px de largura)
    int centerX = 160; 

// --- Configurações de Rede ---
const char* WIFI_SSID = "HKHC-2G";
const char* WIFI_PASS = "h14k04h09c12";
const int UDP_PORT = 20777; // Porta padrão F1

// --- Configurações de Pinos do Display (Não são mais usadas, agora estão no User_Setup.h) ---
// #define PIN_OLED_SCK 18 ... (etc)

// --- Objeto Parser Global ---
F1_25_Parser parser;

// --- Estrutura de Dados Compartilhada (Sem mudanças) ---
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

// --- Protótipos das Tasks ---
void vTask_TelemetryUDP(void *pvParameters);
void vTask_DisplayOLED(void *pvParameters);

// --- Definição da Task de Rede (Sem mudanças) ---
void vTask_TelemetryUDP(void *pvParameters) {
    Serial.println("Task UDP: Iniciando no Core 1...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Task UDP: Conectando ao WiFi");
    while (WiFi.status()!= WL_CONNECTED) {
        Serial.print(".");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println("\nTask UDP: WiFi Conectado!");
    Serial.print("Task UDP: IP do ESP32: ");
    Serial.println(WiFi.localIP()); 

    parser.begin(UDP_PORT);
    Serial.println("Task UDP: Parser F1_25_UDP iniciado, ouvindo porta 20777...");

    while (1) {
        
        uint8_t packetId = parser.read();
        
        if (packetId == 2 || packetId == 6 || packetId == 7 || packetId == 12) {
            
            if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                
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
            } else {
                 Serial.println("Task UDP: Mutex ocupado, dados do parser descartados.");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } // fim while(1)
}

// --- Definição da Task do Display (TOTALMENTE REESCRITA) ---
void vTask_DisplayOLED(void *pvParameters) {
    Serial.println("Task OLED: Iniciando no Core 0...");

    SharedTelemetryData localTelemetry = {0};
    char format_buf[64]; // Buffer para formatar strings
    
    const float MAX_ERS_JOULES = 4000000.0f; 
    
    // Cores para o novo display
    uint16_t bgColor = TFT_BLACK;
    uint16_t textColor = TFT_WHITE;
    uint16_t drsColor = TFT_GREEN;
    uint16_t ersColor = TFT_CYAN;
    uint16_t shiftColor = TFT_RED;

    

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(16); // ~60 FPS
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        // --- Parte 1: Aquisição Segura de Dados ---
        if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&localTelemetry, &g_Telemetry, sizeof(SharedTelemetryData));
            xSemaphoreGive(g_TelemetryMutex);
        } else {
            Serial.println("Task OLED: Mutex ocupado, renderizando dados antigos.");
        }

        // --- Parte 2: Renderização (Layout V16 - TFT_eSPI) ---
        
        // Começa a desenhar (TFT_eSPI desenha item por item, não em buffer)
        tft.fillScreen(bgColor); // Limpa a tela
        
        // --- Tempo da Última Volta (Canto Superior Esquerdo) ---
        tft.setTextColor(textColor, bgColor);
        tft.setTextSize(2); // Fonte tamanho 2
        uint32_t t = localTelemetry.lastLapTimeMS;
        int min = t / 60000;
        int sec = (t % 60000) / 1000;
        int ms = t % 1000;
        sprintf(format_buf, "%d:%02d.%03d", min, sec, ms);
        tft.drawString(format_buf, 5, 5);
        
        // --- Posição e Volta (Canto Superior Direito) ---
        tft.setTextColor(textColor, bgColor);
        tft.setTextSize(2); // Fonte tamanho 2
        sprintf(format_buf, "P%d V%d", localTelemetry.carPosition, localTelemetry.currentLapNum);
        tft.drawRightString(format_buf, 315, 5, 2); // (string, x, y, font_size)

        
        // --- Velocidade (Central, acima da Marcha) ---
        tft.setTextColor(textColor, bgColor);
        tft.setTextSize(5); // Fonte bem maior
        sprintf(format_buf, "%03d", localTelemetry.speed);
        tft.drawCentreString(format_buf, centerX, 60, 4); // (string, x, y, font_size)


        // --- Marcha (Central) ---
        if (localTelemetry.gear == -1) {
            sprintf(format_buf, "R");
        } else if (localTelemetry.gear == 0) {
            sprintf(format_buf, "N");
        } else {
            sprintf(format_buf, "%d", localTelemetry.gear);
        }
        
        // Lógica de inversão
        unsigned long age = millis() - localTelemetry.lastRevLightsTimestamp;
        bool showRevLight = (age < 100) && (localTelemetry.revLightsBitValue >= 2047) && (localTelemetry.rpm > 10000);

        if (showRevLight) {
            // INVERTIDO: Texto preto em fundo vermelho
            tft.setTextColor(TFT_BLACK, shiftColor);
        } else {
            // NORMAL: Texto branco em fundo preto
            tft.setTextColor(textColor, bgColor);
        }
        tft.drawCentreString(format_buf, centerX, 100, 8); // Fonte Gigante (8)
        tft.setTextColor(textColor, bgColor); // Reseta a cor


        // --- RPM (Central, abaixo da Marcha) ---
        tft.setTextSize(2);
        sprintf(format_buf, "%d", localTelemetry.rpm); 
        tft.drawCentreString(format_buf, centerX, 180, 2);
        

        // --- Nível de Bateria ERS (Barra à Esquerda) ---
        tft.drawRect(10, 250, 20, 100, textColor); // Desenha moldura
        float ersPercent = localTelemetry.ersStore / MAX_ERS_JOULES;
        if (ersPercent > 1.0f) ersPercent = 1.0f;
        int barHeight = (int)(ersPercent * 98); // 98 pixels de altura interna
        if (barHeight > 0) {
            tft.fillRect(11, (251 + 98) - barHeight, 18, barHeight, ersColor);
        }
        
        // --- Modo de ERS (Abaixo da barra de ERS) ---
        const char* ersModeStr = "N/A"; 
        switch (localTelemetry.ersDeployMode) {
            case 1: ersModeStr = "MED"; break;
            case 2: ersModeStr = "HL"; break;
            case 3: ersModeStr = "OVT"; break;
        }
        tft.drawCentreString(ersModeStr, 20, 355, 2);

        
        // --- Dados (Meio Direito) - 3 linhas ---
        tft.setTextSize(2); 
        
        sprintf(format_buf, "t_age:%d", localTelemetry.tyresAgeLaps);
        tft.drawRightString(format_buf, 315, 280, 2);

        sprintf(format_buf, "t_left:%d", localTelemetry.tyreLifeSpan);
        tft.drawRightString(format_buf, 315, 305, 2);

        sprintf(format_buf, "fuel:%.1f", localTelemetry.fuelRemainingLaps);
        tft.drawRightString(format_buf, 315, 330, 2);


        // --- Sinalização DRS (Canto Inferior Direito) ---
        if (localTelemetry.drsAllowed) {
            tft.fillRect(250, 250, 65, 25, drsColor);
            tft.setTextColor(TFT_BLACK, drsColor);
            tft.drawCentreString("DRS", 282, 255, 2);
        }

        // --- Parte 3: Atraso Periódico ---
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
    } // fim while(1)
}
// --- Função Setup (Executada uma vez no Core 1) ---
void setup() {
    
    Serial.begin(115200); 
    Serial.println("Iniciando Sistema de Telemetria F1 RTOS (v16 - TFT)...");

    // --- 1. Inicializar Hardware ---
    tft.init();
    tft.setRotation(1); // 0=Retrato, 1=Paisagem, 2=Retrato Invertido, 3=Paisagem Invertida
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawCentreString("Iniciando...", centerX, 100, 2);
    tft.drawCentreString("ESP32 F1 Telemetry", centerX, 130, 2);
    tft.drawCentreString("Conectando ao WiFi...", centerX, 160, 2);

    // --- 2. Inicializar Sincronização ---
    g_TelemetryMutex = xSemaphoreCreateMutex(); 
    if (g_TelemetryMutex == NULL) {
        Serial.println("Erro: Falha ao criar Mutex!");
        while(1) vTaskDelay(1000); // Trava
    }
    Serial.println("Mutex criado com sucesso.");

    // --- 3. Criar as Tasks --- 
    xTaskCreatePinnedToCore(
        vTask_TelemetryUDP, "Task_UDP", 8192, NULL, 5, NULL, 1
    );
    Serial.println("Task UDP criada no Core 1 com Prioridade 5.");

    xTaskCreatePinnedToCore(
        vTask_DisplayOLED, "Task_OLED", 8192, NULL, 1, NULL, 0
    ); // <-- AUMENTEI A STACK DA TASK DE DISPLAY PARA 8192
    Serial.println("Task OLED criada no Core 0 com Prioridade 1.");

    Serial.println("Setup concluído. RTOS escalonando tarefas.");
}

// --- Função Loop (Executada como uma task no Core 1) ---
void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000)); 
    Serial.println("Loop principal (Core 1) ainda vivo...");
}
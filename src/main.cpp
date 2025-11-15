// --- INÍCIO de src/main.cpp (CORRIGIDO) ---

#include <Arduino.h>
#include <WiFi.h>
#include <U8g2lib.h>     // Biblioteca do display
#include <F1_25_UDP.h>   // A biblioteca de parsing de telemetria!

// --- Configurações de Rede ---
const char* WIFI_SSID = "HKHC-2G";
const char* WIFI_PASS = "h14k04h09c12";
const int UDP_PORT = 20777; // Porta padrão F1

// --- Configurações do Display OLED (Hardware SPI) ---
#define PIN_OLED_SCK 18  // (D0)
#define PIN_OLED_MOSI 23 // (D1)
#define PIN_OLED_CS 5
#define PIN_OLED_DC 16
#define PIN_OLED_RES 17

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(
    U8G2_R0,             // Rotação 0
    PIN_OLED_CS,         // CS
    PIN_OLED_DC,         // DC
    PIN_OLED_RES         // RES
);

// --- Objeto Parser Global ---
// **CORREÇÃO 1:** Crie o objeto diretamente, NÃO um ponteiro.
// Isso evita o crash do ponteiro NULL.
F1_25_Parser parser;

// --- Estrutura de Dados Compartilhada e Sincronização ---
struct SharedTelemetryData {
    uint16_t speed;
    int8_t   gear;
    uint32_t currentLapTimeMS;
    uint32_t lastLapTimeMS;
    uint16_t sector1MS;
    uint16_t sector2MS;
    uint8_t  carPosition;
    uint8_t  currentLapNum;
    bool     drsAllowed;
    uint16_t rpm;
    float    ersStore;
    uint8_t  ersDeployMode;
    uint8_t  tyresAgeLaps;       // Idade atual (o que tínhamos)
    float    fuelRemainingLaps;
    uint8_t  tyreLifeSpan;
    uint16_t revLightsBitValue;  // <--- Linha NOVA (é uint16_t)
    unsigned long lastRevLightsTimestamp;
};

SharedTelemetryData g_Telemetry = {0};
SemaphoreHandle_t g_TelemetryMutex;

// --- Protótipos das Tasks ---
void vTask_TelemetryUDP(void *pvParameters);
void vTask_DisplayOLED(void *pvParameters);

// --- Definição da Task de Rede (Core 1) ---
void vTask_TelemetryUDP(void *pvParameters) {
    Serial.println("Task UDP: Iniciando no Core 1...");

    // --- Parte 1: Setup da Task (Conexão WiFi) ---
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Task UDP: Conectando ao WiFi");
    while (WiFi.status()!= WL_CONNECTED) {
        Serial.print(".");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println("\nTask UDP: WiFi Conectado!");
    Serial.print("Task UDP: IP do ESP32: ");
    Serial.println(WiFi.localIP()); 

    // **CORREÇÃO 1:** Usa '.' em vez de '->' porque 'parser' é um objeto.
    parser.begin(UDP_PORT);
    Serial.println("Task UDP: Parser F1_25_UDP iniciado, ouvindo porta 20777...");

    // --- Parte 2: Loop Infinito da Task ---
    while (1) {
        
        // **CORREÇÃO 4:** Chame read() APENAS UMA VEZ.
        // (Isto assume que você modificou a biblioteca F1_25_UDP.h/cpp)
        uint8_t packetId = parser.read();
        
        // **CORREÇÃO 4:** Use o nome correto da variável (sem underscore)
        // Agora nós nos importamos com os pacotes 2, 6, 7 e 12
        if (packetId == 2 || packetId == 6 || packetId == 7 || packetId == 12) {
            
            // --- INÍCIO DA SEÇÃO CRÍTICA ---
            // Tenta pegar o Mutex, mas com paciência de 1ms
            if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                
                // Mutex obtido! Atualiza os dados globais.
                if (packetId == 2) { 
                    // Pacote Lap Data
                    PacketLapData* p = parser.packetLapData();
                    uint8_t carIndex = p->m_playerCarIndex();

                    g_Telemetry.currentLapTimeMS = p->m_lapData(carIndex).m_currentLapTimeInMS;
                    g_Telemetry.lastLapTimeMS = p->m_lapData(carIndex).m_lastLapTimeInMS;
                    g_Telemetry.sector1MS = p->m_lapData(carIndex).m_sector1TimeInMSPart;
                    g_Telemetry.sector2MS = p->m_lapData(carIndex).m_sector2TimeInMSPart;
                    g_Telemetry.carPosition = p->m_lapData(carIndex).m_carPosition;
                    g_Telemetry.currentLapNum = p->m_lapData(carIndex).m_currentLapNum;
                    

                } else if (packetId == 6) { 
                    // Pacote Car Telemetry
                    PacketCarTelemetryData* p = parser.packetCarTelemetryData();
                    uint8_t carIndex = p->m_playerCarIndex();

                    g_Telemetry.speed = p->m_carTelemetryData(carIndex).m_speed;
                    g_Telemetry.gear = p->m_carTelemetryData(carIndex).m_gear;
                    g_Telemetry.drsAllowed = (p->m_carTelemetryData(carIndex).m_drs == 1);
                    g_Telemetry.rpm = p->m_carTelemetryData(carIndex).m_engineRPM; 
                    g_Telemetry.revLightsBitValue = p->m_carTelemetryData(carIndex).m_revLightsBitValue;
                    // **AQUI ESTÁ A CORREÇÃO**
                    // Salva o "carimbo de data/hora" exato de quando lemos este dado
                    g_Telemetry.lastRevLightsTimestamp = millis();
                } else if (packetId == 7) {
                    // Pacote Car Status
                    PacketCarStatusData* p = parser.packetCarStatusData();
                    uint8_t carIndex = p->m_playerCarIndex();
                    
                    g_Telemetry.ersStore = p->m_carStatusData(carIndex).m_ersStoreEnergy;
                    g_Telemetry.ersDeployMode = p->m_carStatusData(carIndex).m_ersDeployMode;
                    g_Telemetry.tyresAgeLaps = p->m_carStatusData(carIndex).m_tyresAgeLaps;
                    g_Telemetry.fuelRemainingLaps = p->m_carStatusData(carIndex).m_fuelRemainingLaps;
                    

                } else if (packetId == 12) {
                    // Pacote Tyre Set Data
                    PacketTyreSetData* p = parser.packetTyreSetData();
                    uint8_t carIndex = p->m_playerCarIndex();

                    g_Telemetry.tyreLifeSpan = p->m_tyresetData(carIndex).m_lifeSpan;
                    
                }
                
                // Libera o Mutex
                xSemaphoreGive(g_TelemetryMutex);
                // --- FIM DA SEÇÃO CRÍTICA ---

            } else {
                // Não precisa imprimir, só significa que o display estava ocupado
                // e este pacote (antigo) será descartado.
            }
        }
        
        // Cede o menor tempo possível para o escalonador
        vTaskDelay(pdMS_TO_TICKS(1));

    } // fim while(1)
}

// --- Definição da Task do Display (Core 0) ---

// --- Definição da Task do Display (Core 0) ---
void vTask_DisplayOLED(void *pvParameters) {
    Serial.println("Task OLED: Iniciando no Core 0...");

    SharedTelemetryData localTelemetry = {0};
    char format_buf[64]; // Buffer para formatar strings
    
    const float MAX_ERS_JOULES = 4000000.0f; 

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(16); // 30 FPS
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        // --- Parte 1: Aquisição Segura de Dados ---
        if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(&localTelemetry, &g_Telemetry, sizeof(SharedTelemetryData));
            xSemaphoreGive(g_TelemetryMutex);
            
        } else {
            Serial.println("Task OLED: Mutex ocupado, renderizando dados antigos.");
        }

        // --- Parte 2: Renderização (Layout V13 - Marcha Invertida) ---
        
        u8g2.clearBuffer();
        
        // --- Tempo da Última Volta (Canto Superior Esquerdo) ---
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        uint32_t t = localTelemetry.lastLapTimeMS;
        int min = t / 60000;
        int sec = (t % 60000) / 1000;
        int ms = t % 1000;
        sprintf(format_buf, "%d:%02d.%03d", min, sec, ms);
        u8g2.drawStr(0, 10, format_buf);
        
        // --- Posição e Volta (Canto Superior Direito) ---
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        sprintf(format_buf, "P%d V%d", localTelemetry.carPosition, localTelemetry.currentLapNum);
        int pos_lap_width = u8g2.getStrWidth(format_buf);
        u8g2.drawStr(128 - pos_lap_width - 2, 10, format_buf);

        
        // --- Velocidade (Central, acima da Marcha) ---
        u8g2.setFont(u8g2_font_ncenB12_tr); 
        sprintf(format_buf, "%03d", localTelemetry.speed);
        int speed_width = u8g2.getStrWidth(format_buf);
        u8g2.drawStr((128 - speed_width) / 2, 20, format_buf);


        // --- Marcha (Central) ---
        u8g2.setFont(u8g2_font_logisoso28_tr); 
        
        // Prepara o texto da marcha
        if (localTelemetry.gear == -1) {
            sprintf(format_buf, "R");
        } else if (localTelemetry.gear == 0) {
            sprintf(format_buf, "N");
        } else {
            sprintf(format_buf, "%d", localTelemetry.gear);
        }
        int gear_width = u8g2.getStrWidth(format_buf);
        int gear_x = (128 - gear_width) / 2;

        // **AQUI ESTÁ A CORREÇÃO FINAL**
        // 1. Verifica se o dado é "novo" (menos de 100ms de idade)
        unsigned long age = millis() - localTelemetry.lastRevLightsTimestamp;
        bool showRevLight = (age < 100) && (localTelemetry.revLightsBitValue >= 2047) && (localTelemetry.rpm > 10000);

        if (showRevLight) {
            // INVERTIDO: Desenha uma caixa branca e texto preto
            u8g2.drawBox(gear_x - 2, 26, gear_width + 4, 28); // Fundo branco
            u8g2.setFontMode(0);    // Modo transparente
            u8g2.setDrawColor(0);   // Texto PRETO
            u8g2.drawStr(gear_x, 50, format_buf);
            u8g2.setDrawColor(1);   // Reseta cor para BRANCO
        } else {
            // NORMAL: Desenha texto branco
            u8g2.drawStr(gear_x, 50, format_buf);
        }
        // --- RPM (Central, abaixo da Marcha) ---
        u8g2.setFont(u8g2_font_ncenB08_tr);
        
        // Lógica de inversão REMOVIDA daqui
        sprintf(format_buf, "%d", localTelemetry.rpm); 
        int rpm_width = u8g2.getStrWidth(format_buf);
        int rpm_x = (128 - rpm_width) / 2;
        u8g2.drawStr(rpm_x, 64, format_buf);
        

        // --- Nível de Bateria ERS (Barra à Esquerda) ---
        u8g2.drawFrame(5, 12, 8, 42); 
        float ersPercent = localTelemetry.ersStore / MAX_ERS_JOULES;
        if (ersPercent > 1.0f) ersPercent = 1.0f;
        if (ersPercent < 0.0f) ersPercent = 0.0f;
        int barHeight = (int)(ersPercent * 40); 
        if (barHeight > 0) {
            u8g2.drawBox(6, (12 + 41) - barHeight, 6, barHeight);
        }

        
        // --- Modo de ERS (Abaixo da barra de ERS) ---
        u8g2.setFont(u8g2_font_ncenB08_tr);
        const char* ersModeStr = "N/A"; 
        switch (localTelemetry.ersDeployMode) {
            case 1: ersModeStr = "MED"; break;
            case 2: ersModeStr = "HL"; break;
            case 3: ersModeStr = "OVT"; break;
        }
        u8g2.drawStr(4, 64, ersModeStr);

        
        // --- Dados (Meio Direito) - 3 linhas (Seu layout V12) ---
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        
        sprintf(format_buf, "t_age:%d", localTelemetry.tyresAgeLaps);
        int tyre_age_width = u8g2.getStrWidth(format_buf);
        u8g2.drawStr(128 - tyre_age_width - 2, 26, format_buf);

        sprintf(format_buf, "t_left:%d", localTelemetry.tyreLifeSpan);
        int tyre_life_width = u8g2.getStrWidth(format_buf);
        u8g2.drawStr(128 - tyre_life_width - 2, 38, format_buf);

        sprintf(format_buf, "fuel:%.1f", localTelemetry.fuelRemainingLaps);
        int fuel_width = u8g2.getStrWidth(format_buf);
        u8g2.drawStr(128 - fuel_width - 2, 50, format_buf);


        // --- Sinalização DRS (Canto Inferior Direito) ---
        if (localTelemetry.drsAllowed) {
            u8g2.setFont(u8g2_font_ncenB10_tr); 
            u8g2.drawBox(92, 52, 30, 12);
            u8g2.setFontMode(0);
            u8g2.setDrawColor(0);
            u8g2.drawStr(94, 62, "DRS");
            u8g2.setDrawColor(1);
        }

        // Envia o buffer renderizado para o display
        u8g2.sendBuffer();

        // --- Parte 3: Atraso Periódico ---
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
    } // fim while(1)
}
// --- Função Setup (Executada uma vez no Core 1) ---
void setup() {
    // **CORREÇÃO 1:** Removido 'parser->begin(20777);'
    // A task de UDP fará isso após conectar ao WiFi.
    
    Serial.begin(115200); 
    Serial.println("Iniciando Sistema de Telemetria F1 RTOS (v6 - Corrigido)...");

    // --- 1. Inicializar Hardware ---
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Iniciando...");
    u8g2.drawStr(0, 25, "ESP32 F1 Telemetry");
    u8g2.drawStr(0, 40, "Conectando ao WiFi...");
    u8g2.sendBuffer();

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
        vTask_DisplayOLED, "Task_OLED", 4096, NULL, 1, NULL, 0
    );
    Serial.println("Task OLED criada no Core 0 com Prioridade 1.");

    Serial.println("Setup concluído. RTOS escalonando tarefas.");
}

// --- Função Loop (Executada como uma task no Core 1) ---
void loop() {
    // **CORREÇÃO 1:** Removido 'parser = new F1_25_Parser();' 
    // **CORREÇÃO 1:** Removido 'parser->read();' (A task já faz isso)

    // O loop principal cede seu tempo e não faz mais nada.
    vTaskDelay(pdMS_TO_TICKS(5000)); 
    Serial.println("Loop principal (Core 1) ainda vivo...");
}
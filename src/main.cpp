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
        if (packetId == 2 || packetId == 6) {
            
            // --- INÍCIO DA SEÇÃO CRÍTICA ---
            if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                
                // Mutex obtido! Atualiza os dados globais.
                if (packetId == 2) { 
                    // Pacote Lap Data
                    PacketLapData* p = parser.packetLapData();
                    
                    // **CORREÇÃO 2:** Use o getter PÚBLICO, não acesse o header PRIVADO.
                    uint8_t carIndex = p->m_playerCarIndex();

                    // Acessa os dados como uma FUNÇÃO(carIndex)
                    g_Telemetry.currentLapTimeMS = p->m_lapData(carIndex).m_currentLapTimeInMS;
                    g_Telemetry.lastLapTimeMS = p->m_lapData(carIndex).m_lastLapTimeInMS;
                    g_Telemetry.sector1MS = p->m_lapData(carIndex).m_sector1TimeInMSPart;
                    g_Telemetry.sector2MS = p->m_lapData(carIndex).m_sector2TimeInMSPart;
                    g_Telemetry.carPosition = p->m_lapData(carIndex).m_carPosition;
                    g_Telemetry.currentLapNum = p->m_lapData(carIndex).m_currentLapNum;

                } else if (packetId == 6) { 
                    // Pacote Car Telemetry
                    PacketCarTelemetryData* p = parser.packetCarTelemetryData();
                    
                    // **CORREÇÃO 2:** Use o getter PÚBLICO.
                    uint8_t carIndex = p->m_playerCarIndex();

                    // Acessa os dados como uma FUNÇÃO(carIndex)
                    g_Telemetry.speed = p->m_carTelemetryData(carIndex).m_speed;
                    g_Telemetry.gear = p->m_carTelemetryData(carIndex).m_gear;
                    g_Telemetry.drsAllowed = (p->m_carTelemetryData(carIndex).m_drs == 1);
                }
                
                // Libera o Mutex
                xSemaphoreGive(g_TelemetryMutex);
                // --- FIM DA SEÇÃO CRÍTICA ---

            } else {
                Serial.println("Task UDP: Mutex ocupado, dados do parser descartados.");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));

    } // fim while(1)
}

// --- Definição da Task do Display (Core 0) ---
void vTask_DisplayOLED(void *pvParameters) {
    Serial.println("Task OLED: Iniciando no Core 0...");

    SharedTelemetryData localTelemetry = {0};

    // **CORREÇÃO 3:** Crie um array (buffer) de tamanho fixo.
    // 64 bytes é mais que o suficiente para as linhas do display.
    char format_buf[64]; 

    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // 10 FPS
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        // --- Parte 1: Aquisição Segura de Dados ---
        if (xSemaphoreTake(g_TelemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            memcpy(&localTelemetry, &g_Telemetry, sizeof(SharedTelemetryData));
            xSemaphoreGive(g_TelemetryMutex);
        } else {
            Serial.println("Task OLED: Mutex ocupado, renderizando dados antigos.");
        }

        // --- Parte 2: Renderização (Lenta) ---
        
        u8g2.clearBuffer(); 
        
        // --- Linha 1: Velocidade e Marcha ---
        u8g2.setFont(u8g2_font_ncenB10_tr); 
        // **CORREÇÃO 3:** 'format_buf' agora é um buffer válido.
        sprintf(format_buf, "Vel: %03d", localTelemetry.speed);
        u8g2.drawStr(0, 12, format_buf);
        
        sprintf(format_buf, "M: %d", localTelemetry.gear);
        u8g2.drawStr(90, 12, format_buf);

        // --- Linha 2: Tempo de Volta ---
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        uint32_t t = localTelemetry.currentLapTimeMS;
        int min = t / 60000;
        int sec = (t % 60000) / 1000;
        int ms = t % 1000;
        sprintf(format_buf, "Volta: %d:%02d.%03d", min, sec, ms);
        u8g2.drawStr(0, 30, format_buf);

        // --- Linha 3: Posição e Volta Atual ---
        sprintf(format_buf, "Pos: %d / Lap: %d", localTelemetry.carPosition, localTelemetry.currentLapNum);
        u8g2.drawStr(0, 44, format_buf);

        // --- Linha 4: Tempos de Setor (Exemplo) ---
        sprintf(format_buf, "S1: %d S2: %d", localTelemetry.sector1MS, localTelemetry.sector2MS);
        u8g2.drawStr(0, 58, format_buf);

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
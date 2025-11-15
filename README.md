# Projeto de Telemetria F1 25 com ESP32 e RTOS

**SEL0337 - PROJETOS EM SISTEMAS EMBARCADOS**
**Prática 6: Introdução aos Sistemas Operacionais de Tempo Real (RTOS)**

- **Aluno:** Heitor Kaito Oumura        
- **Nº USP:** 13828101
- **Aluno:** Gabriel Breda Coelho       
- **Nº USP:** 14746360
---

## 1. Resumo do Projeto

Este projeto implementa um display de telemetria em tempo real para o jogo F1 25. O sistema é centralizado em um microcontrolador ESP32, que se conecta à rede Wi-Fi local para receber pacotes de dados UDP enviados pelo jogo.

Utilizando o sistema operacional de tempo real FreeRTOS, o projeto gerencia de forma eficiente duas tarefas concorrentes para processar e exibir os dados, demonstrando conceitos-chave de sistemas embarcados, multitarefa e sincronização.

As informações exibidas no display OLED incluem:
-Velocidade atual
-Marcha atual
-Tempo da volta atual
-Posição na corrida
-E outros dados de telemetria...

## 2. Conceitos da Prática 6 (RTOS) Implementados

Este projeto atende a todos os requisitos da prática, demonstrando o uso avançado do FreeRTOS para gerenciar tarefas concorrentes.

### Multitarefa e Processamento Multinúcleo

O sistema é dividido em duas tarefas principais, cada uma fixada em um núcleo do processador dual-core do ESP32 para garantir performance e previsibilidade:

* `vTask_TelemetryUDP` (**Núcleo 1**, **Prioridade 5 - Alta**):
    * Responsável por toda a comunicação de rede.
    * Gerencia a conexão Wi-Fi e "ouve" a porta UDP (20777) esperando por pacotes do jogo.
    * Ao receber um pacote, ela adquire o mutex e atualiza a estrutura de dados global `g_Telemetry`.
    * Foi definida como alta prioridade para garantir que nenhum pacote UDP seja perdido (é uma tarefa de Entrada/Saída crítica).

* `vTask_DisplayOLED` (**Núcleo 0**, **Prioridade 1 - Baixa**):
    * Responsável por toda a interface com o usuário.
    * Executa em um loop periódico (controlado por `vTaskDelayUntil`) para atualizar o display OLED a uma taxa constante (10 FPS).
    * Antes de desenhar na tela, ela adquire o mutex e faz uma cópia local dos dados da `g_Telemetry` para exibir.
    * Foi definida como baixa prioridade, pois uma pequena variação na taxa de atualização do display não é crítica para o sistema.

### Sincronização com Mutex (Exclusão Mútua)

O recurso mais crítico deste projeto é a variável global `SharedTelemetryData g_Telemetry`, que é compartilhada entre as duas tarefas. Para evitar uma condição de corrida, foi implementado um Mutex (`g_TelemetryMutex`).

O Mutex (Mutual Exclusion) garante que apenas uma tarefa possa acessar a variável `g_Telemetry` de cada vez, assegurando a integridade e consistência dos dados em todo o sistema.

---

## 3. Diagrama Esquemático e Montagem

A montagem utiliza um ESP32 DevKit V1 e um display OLED de 0.96" (SH1106) conectado via interface SPI.

### Diagrama Esquemático

*[INSIRA AQUI UMA IMAGEM DO SEU DIAGRAMA ESQUEMÁTICO (Fritzing ou rascunho)]*
### Foto da Montagem Prática
![alt text](image.png)
---

## 4. Funcionamento e Resultados

O sistema foi capaz de receber e exibir os dados de telemetria do F1 25 com sucesso. A configuração do jogo necessária é:

* **Formato UDP:** 2025
* **Porta UDP:** 20777
* **Endereço IP UDP:** [O IP do seu ESP32]
* **Taxa de Envio:** 60Hz

### Vídeo de Funcionamento

*[INSIRA AQUI O LINK PARA UM VÍDEO DO PROJETO FUNCIONANDO COM O JOGO]*

### Fotos do Display em Ação

*[INSIRA AQUI FOTOS DO DISPLAY MOSTRANDO A TELEMETRIA EM TEMPO REAL]*

---

## 5. Diferença: Tasks (RTOS) vs. Threads/Processos (Linux)

Conforme solicitado, a principal diferença entre as **Tasks** do FreeRTOS e os **Processos/Threads** de um S.O. de propósito geral (como o Linux) é o **determinismo e o scheduler**.

* **Processos (Linux):** São "pesados" e possuem espaços de memória completamente isolados. A troca de contexto é lenta.
* **Threads (Linux):** São "leves" e compartilham o mesmo espaço de memória de um processo. A troca é mais rápida, mas o *scheduler* do Linux é projetado para **equidade** e **throughput** (processar o máximo de coisas ao longo do tempo), sem garantias estritas de *quando* uma thread específica irá rodar.
* **Tasks (FreeRTOS):** São extremamente "leves" e compartilham memória (como threads). A diferença fundamental é o **scheduler preemptivo baseado em prioridade estrita**. O FreeRTOS *garante* que, a qualquer momento, a tarefa de maior prioridade que está pronta para rodar **irá rodar imediatamente**, mesmo que isso signifique "fazer esperar" tarefas de prioridade mais baixa.

Em resumo, o Linux é otimizado para *performance média*, enquanto o FreeRTOS é otimizado para **determinismo** e **cumprimento de prazos** (real-time), o que é essencial para este projeto, onde perder um pacote UDP (tarefa de alta prioridade) é mais crítico do que atrasar um frame do display (tarefa de baixa prioridade).
/* Projeto: Prototipo de Medidor de Vazão e Pressão com ESP32


integrantes do grupo:
- Joao Pedro Marucci Pagliuso
- Gabriel Romão Ikegami
- Victor Thiago BArbosa


Descrição: Este código é um protótipo de um medidor de vazão e pressão utilizando um ESP32.
Ele lê os valores de pressão e vazão, gera pulsos para simular a vazão,e envia os dados
 para o ThingSpeak a cada 15 segundos.Além disso,exibe as informações em um display LCD I2C.


=========================================================================================*/

// Bibliotecas utilizadas: 
#include <WiFi.h>
/*1. WiFi.h:
  - Biblioteca padrão do ESP32 para conexão Wi-Fi.
  - Permite conectar o dispositivo a redes Wi-Fi e gerenciar a comunicação sem fio.*/
#include <HTTPClient.h>
/*2. HTTPClient.h:
  - Biblioteca para realizar requisições HTTP.
  - Utilizada para enviar dados ao servidor ThingSpeak via protocolo HTTP.*/
#include <Wire.h>
/*3. Wire.h:
  - Biblioteca para comunicação I2C.
  - Necessária para a comunicação entre o ESP32 e o display LCD I2C.*/
#include <LiquidCrystal_I2C.h>
/*4. LiquidCrystal_I2C.h:
  - Biblioteca para controle de displays LCD com interface I2C.
  - Facilita a exibição de informações no display, como pressão, fluxo e volume.

=========================================================================================*/


// Inicializa o display LCD no endereço I2C 0x27, com 16 colunas e 2 linhas
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Endereço comum: 0x27 ou 0x3F


// Define os pinos utilizados para sensores e pulsos
#define PINO_PRESSAO 35            // Pino analógico que lê a pressão
#define PINO_VAZAO 34              // Pino analógico que lê o sinal do potenciômetro de vazão
#define GPIO_PULSOS 4              // Pino digital que gera pulsos
#define GPIO_CONTADOR 5            // Pino digital que recebe os pulsos e conta


// Definições do Wi-Fi
// Substitua pelos dados da sua rede Wi-Fi
const char* ssid = "";
const char* password = "";

// Definições do ThingSpeak
// Substitua pela sua chave de API do ThingSpeak
const char* server = "http://api.thingspeak.com/update";
String apiKey = "sua chave de acesso";

// Variáveis globais para medição
volatile unsigned long contador_pulsos = 0;              // Contador auxiliar
volatile unsigned long contador_acionamentos = 0;        // Contador de pulsos (incrementado por interrupção)
const float FATOR_CALIBRACAO = 4.5;                      // Fator para conversão de pulsos em L/min

// Variáveis de controle
float fluxo = 0;             // Fluxo em L/min
float volume = 0;            // Volume parcial (em L)
float volume_total = 0;      // Volume total acumulado (em L)
float pressao_volts = 0;     // Tensão lida no sensor de pressão
float pressao_bar = 0;       // Pressão convertida em bar

// Variáveis de tempo
unsigned long tempo_antes = 0;     // Marca tempo para cálculo de fluxo
unsigned long tempo_pulso = 0;     // Marca tempo para geração de pulso
unsigned long tempo_envio = 0;     // Marca tempo para envio ao ThingSpeak
bool estadoPulso = false;          // Estado do pino de pulso (HIGH ou LOW)

/// Função de interrupção para contar os pulsos
void IRAM_ATTR funcao_ISR() {
  contador_acionamentos++;         // Incrementa o contador sempre que um pulso é detectado
}


void setup() {
  Serial.begin(115200);                            // Inicia comunicação serial

  pinMode(GPIO_PULSOS, OUTPUT);                    // Define o pino de pulsos como saída
  digitalWrite(GPIO_PULSOS, LOW);                  // Inicializa o pino com nível baixo

  pinMode(GPIO_CONTADOR, INPUT_PULLUP);            // Pino do contador como entrada com pull-up
  attachInterrupt(GPIO_CONTADOR, funcao_ISR, RISING); // Configura interrupção na borda de subida

  tempo_antes = millis();                          // Inicializa tempo de fluxo
  tempo_envio = millis();                          // Inicializa tempo de envio

  WiFi.begin(ssid, password);                      // Inicia conexão Wi-Fi
  Serial.print("Conectando ao Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {          // Espera conexão
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi conectado!");

  lcd.init();                                       // Inicializa LCD
  lcd.backlight();                                  // Liga backlight
  lcd.setCursor(0, 0);                              // Move cursor
  lcd.print("Inicializando...");                    // Mensagem inicial
  delay(1500);                                      // Aguarda um tempo para leitura da mensagem
}


// Loop principal
// Leitura da pressão e vazão, geração de pulsos e envio para ThingSpeak
void loop() {
  // --- Leitura da pressão ---
  int leitura_pressao = analogRead(PINO_PRESSAO);               // Lê valor analógico da pressão
  pressao_volts = (leitura_pressao / 4095.0) * 3.3;             // Converte para tensão (0–3.3V)
  pressao_bar = (pressao_volts / 3.3) * 10.0;                   // Converte para bar (supondo sensor 0–10 bar)

  // --- Leitura da vazão via potenciômetro ---
  int leitura_vazao = analogRead(PINO_VAZAO);                   // Lê valor analógico da vazão
  float fator_desejo_vazao = leitura_vazao / 4095.0;            // Normaliza entre 0 e 1
  float fator_pressao = pressao_bar / 10.0;                     // Normaliza a pressão
  float fator_real_vazao = fator_desejo_vazao * pow(fator_pressao, 1.5); // Combina vazão com pressão (não linear)

  // Define o intervalo de pulso com base na vazão (inversamente proporcional)
  int intervalo_pulso = map(fator_real_vazao * 1000, 0, 1000, 100, 2); // Tempo entre pulsos

  // --- Geração de pulsos ---
  if ((millis() - tempo_pulso) >= intervalo_pulso) {            // Se passou tempo suficiente
    estadoPulso = !estadoPulso;                                 // Inverte estado do pino
    digitalWrite(GPIO_PULSOS, estadoPulso);                     // Escreve estado no pino
    tempo_pulso = millis();                                     // Atualiza tempo do último pulso
  }

  // --- Cálculo do fluxo e volume a cada segundo ---
  if ((millis() - tempo_antes) > 1000) {                        // Se passou 1 segundo
    detachInterrupt(GPIO_CONTADOR);                            // Desativa interrupção para evitar conflito
    fluxo = ((1000.0 / (millis() - tempo_antes)) * contador_acionamentos) / FATOR_CALIBRACAO; // Calcula fluxo (L/min)
    volume = fluxo / 60;                                       // Calcula volume parcial (L)
    volume_total += volume;                                    // Soma ao volume total
    contador_acionamentos = 0;                                 // Reseta o contador de pulsos
    tempo_antes = millis();                                    // Atualiza tempo

    // --- Exibe os dados no monitor serial ---
    Serial.print("Fluxo: ");
    Serial.print(fluxo, 2);
    Serial.print(" L/min | Volume: ");
    Serial.print(volume_total, 2);
    Serial.print(" L | Pressao: ");
    Serial.print(pressao_bar, 2);
    Serial.println(" bar");

    // --- Atualiza LCD com dados ---
    lcd.clear();                                               // Limpa tela
    lcd.setCursor(0, 0);                                       // Primeira linha
    lcd.print("P:");
    lcd.print(pressao_bar, 1);                                 // Mostra pressão
    lcd.print("b F:");
    lcd.print(fluxo, 1);                                       // Mostra fluxo
    lcd.print("L");

    lcd.setCursor(0, 1);                                       // Segunda linha
    lcd.print("Vol:");
    lcd.print(volume_total, 1);                                // Mostra volume total
    lcd.print(" L");

    attachInterrupt(GPIO_CONTADOR, funcao_ISR, RISING);        // Reativa interrupção
  }

  // --- Envio ao ThingSpeak a cada 15 segundos ---
  if (millis() - tempo_envio > 15000) {
    if (WiFi.status() == WL_CONNECTED) {                       // Verifica conexão Wi-Fi
      HTTPClient http;                                         // Cria cliente HTTP
      String url = server;
      url += "?api_key=" + apiKey;                             // Adiciona chave da API
      url += "&field1=" + String(fluxo, 2);                    // Campo 1: fluxo
      url += "&field2=" + String(volume_total, 2);             // Campo 2: volume total
      url += "&field3=" + String(pressao_bar, 2);              // Campo 3: pressão

      http.begin(url);                                         // Inicia requisição HTTP
      int httpResponseCode = http.GET();                       // Envia requisição GET
      http.end();                                              // Finaliza conexão

      if (httpResponseCode > 0) {                              // Se a resposta for válida
        Serial.println("Enviado ao ThingSpeak!");              // Confirma envio
      } else {
        Serial.print("Erro: ");                                // Exibe erro
        Serial.println(httpResponseCode);
      }
    }
    tempo_envio = millis();                                    // Atualiza tempo do último envio

  }
}

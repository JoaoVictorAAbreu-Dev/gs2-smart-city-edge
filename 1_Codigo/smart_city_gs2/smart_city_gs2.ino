// =====================================================
// GS 2026 - Smart City: cruzamento com cancelas e semáforos
// Placa: ESP32S3 Dev Module (propriedades da seção 06 da prova)
// Bibliotecas: U8g2, ESP32Servo, ArduinoJson (v7)
// Arquivo: secrets.h na mesma pasta (OPENWEATHER_API_KEY)
// =====================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include "secrets.h"

// =====================================
// REDE E OTA (credenciais e servidor NÃO ALTERADOS)
// =====================================
const char* ssid  = "iMorgs!";
const char* senha = "10203040";
String servidor   = "https://serverota.up.railway.app";

// Identificação do nosso firmware (aparece no Serial).
// A decisão de atualizar NÃO depende mais deste número: ver "CONTROLE DE OTA".
int versao = 3;

// Coloque false para testar pelo USB sem que o servidor troque o firmware.
// O .bin entregue ao professor deve ser gerado com true.
const bool OTA_ATIVO = true;
const unsigned long INTERVALO_OTA = 10000;   // ms (igual ao código base)

unsigned long tempo = 0;            // última verificação de OTA
unsigned long tempoReconexao = 0;   // última tentativa de reconectar o Wi-Fi

// =====================================
// PINAGEM (tabela da página 2 da prova)
// =====================================
const int SEM1_VERMELHO = 4;
const int SEM1_AMARELO  = 5;
const int SEM1_VERDE    = 6;
const int SEM2_VERMELHO = 12;
const int SEM2_AMARELO  = 13;
const int SEM2_VERDE    = 14;
const int SERVO1_PIN    = 15;
const int SERVO2_PIN    = 16;
const int LCD_SDA       = 8;
const int LCD_SCL       = 9;

// Melhoria adicional (GPIOs livres na tabela; nada da maquete usa estes pinos)
const int BUZZER_PIN          = 7;
const int SENSOR_PEDESTRE_PIN = 10;

// O sensor não é instalado nesta avaliação. Com false, o pino nem é lido
// (um pino solto "flutua" e geraria detecções falsas). Mudar para true ao instalar.
const bool SENSOR_PEDESTRE_ATIVO = false;

// =====================================
// CANCELAS
// Teste na maquete: aumentar o ângulo faz o braço DESCER, então a posição
// normal (abaixada, bloqueando a via) é 90° e levantada é 0°.
// Na versão anterior estes valores estavam trocados: a cancela começava
// levantada e "abrir" fazia o braço descer.
// Ângulos separados por cancela: se só uma se mover ao contrário,
// troque os dois valores DELA entre si. Ajuste fino de 5 em 5 graus,
// sem deixar o braço encostar na estrutura.
// =====================================
const int CANCELA1_FECHADA = 90;   // abaixada (posição normal)
const int CANCELA1_ABERTA  = 0;    // levantada
const int CANCELA2_FECHADA = 90;
const int CANCELA2_ABERTA  = 0;

// "abertura" vai de 0 (fechada) a 100 (aberta). Cada passo move ~0,9 grau,
// então o curso completo leva 100 x 20 ms = 2 s (movimento lento e gradual).
const unsigned long INTERVALO_PASSO_CANCELA = 20;   // ms entre passos

Servo cancela1;
Servo cancela2;
int abertura = 0;
unsigned long ultimoPassoCancela = 0;

// =====================================
// TEMPOS DO CICLO (ms)
// =====================================
const unsigned long T_VERDE            = 8000;
const unsigned long T_AMARELO          = 2000;
// Folga curta: a cancela acompanha o farol (sobe logo após o vermelho
// e o verde acende logo após ela terminar de descer).
const unsigned long T_TODOS_VERMELHO   = 1000;
const unsigned long T_CANCELAS_ABERTAS = 6000;

// Tratamento do sensor de pedestre
const unsigned long T_CONFIRMA_PEDESTRE = 200;     // leitura precisa ficar estável por este tempo
const unsigned long T_MAX_ESPERA_PEDESTRE = 20000; // sensor travado não pode parar a cidade

// =====================================
// MÁQUINA DE ESTADOS DO CRUZAMENTO
// Fluxo 1 = cancelas. Fluxo 2 = semáforos.
// Regras de segurança garantidas pela ordem dos estados:
//  - semáforo verde só com as cancelas totalmente fechadas
//  - cancelas só se movem com os dois semáforos no vermelho
// =====================================
enum Estado {
  VERDE_FLUXO2,
  AMARELO_FLUXO2,
  VERMELHO_ANTES_CANCELAS,
  ABRINDO_CANCELAS,
  CANCELAS_ABERTAS,
  FECHANDO_CANCELAS,
  VERMELHO_DEPOIS_CANCELAS
};

// Começa no estado mais seguro: tudo vermelho e cancelas fechadas.
Estado estado = VERMELHO_DEPOIS_CANCELAS;
unsigned long tempoEstado = 0;

// =====================================
// LCD E CLIMA
// =====================================
// Construtor com (rotação, reset, clock, data): a própria U8g2 inicia o I2C nesses pinos.
U8G2_ST7567_OS12864_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, LCD_SCL, LCD_SDA);

struct Clima {
  bool valido = false;    // true depois da primeira leitura bem-sucedida
  float temperatura = 0;  // graus Celsius
  int umidade = 0;        // %
};
Clima clima;

const char* CIDADE = "Sao%20Paulo,BR";              // %20 = espaço na URL
const unsigned long INTERVALO_CLIMA_OK   = 600000;  // 10 min após sucesso
const unsigned long INTERVALO_CLIMA_ERRO = 60000;   // 1 min após falha
unsigned long ultimaConsultaClima = 0;
unsigned long intervaloClima = 0;                   // 0 = consultar assim que houver Wi-Fi

// =====================================
// CONTROLE DE OTA
// Problema do código base: ele comparava version.txt com um número fixo no
// firmware. Se o número do servidor fosse maior, o aparelho baixava o mesmo
// firmware e reiniciava a cada 10 s, para sempre (as cancelas nunca abriam).
// Solução: guardar na memória flash (NVS) a última versão do servidor já
// tratada. O firmware recém-gravado adota a versão atual do servidor como
// referência e só atualiza quando o professor publicar uma versão MAIOR.
// =====================================
Preferences memoria;
int versaoServidorConhecida = 0;
bool firmwareRecemGravado = false;

void iniciarControleOTA() {
  memoria.begin("ota", false);
  // O MD5 identifica o firmware gravado. Se mudou, este é o primeiro boot dele.
  firmwareRecemGravado = (memoria.getString("md5", "") != ESP.getSketchMD5());
  versaoServidorConhecida = memoria.getInt("versao", 0);
}

void atualizar(int novaVersao) {
  WiFiClientSecure client;
  client.setInsecure();

  Serial.println("Atualizando...");

  // Reinício manual: a versão só é gravada na memória se o download deu certo.
  httpUpdate.rebootOnUpdate(false);
  if (httpUpdate.update(client, servidor + "/firmware.bin") == HTTP_UPDATE_OK) {
    memoria.putInt("versao", novaVersao);
    ESP.restart();
  }
  Serial.println("Falha na atualizacao OTA. Nova tentativa no proximo ciclo.");
}

void verificarAtualizacao() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, servidor + "/version.txt");
  http.setTimeout(4000);

  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {   // servidor fora do ar: ignora e tenta depois
    http.end();
    return;
  }
  int novaVersao = http.getString().toInt();
  http.end();

  if (novaVersao <= 0) return;    // resposta vazia ou inválida

  if (firmwareRecemGravado) {
    // Primeiro contato deste firmware com o servidor: só registra a referência.
    memoria.putInt("versao", novaVersao);
    memoria.putString("md5", ESP.getSketchMD5());
    versaoServidorConhecida = novaVersao;
    firmwareRecemGravado = false;
    Serial.printf("OTA: versao %d do servidor registrada como referencia\n", novaVersao);
    return;
  }

  if (novaVersao > versaoServidorConhecida) {
    Serial.println("Nova versao encontrada!");
    atualizar(novaVersao);
  }
}

// =====================================
// WI-FI
// =====================================

// Espera no máximo timeoutMs. Sem rede, o cruzamento funciona normalmente:
// só o clima e a OTA dependem de internet.
void conectarWiFi(unsigned long timeoutMs) {
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(ssid, senha);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi conectado! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi indisponivel. Controle local continua ativo.");
  }
}

// =====================================
// SEMÁFOROS
// =====================================

// Os dois semáforos controlam o mesmo fluxo, então mostram sempre a mesma cor.
void setSemaforos(bool vermelho, bool amarelo, bool verde) {
  digitalWrite(SEM1_VERMELHO, vermelho);
  digitalWrite(SEM1_AMARELO, amarelo);
  digitalWrite(SEM1_VERDE, verde);
  digitalWrite(SEM2_VERMELHO, vermelho);
  digitalWrite(SEM2_AMARELO, amarelo);
  digitalWrite(SEM2_VERDE, verde);
}

// =====================================
// CANCELAS
// =====================================

// Converte "abertura" (0 a 100) no ângulo de cada servo e envia a posição.
void posicionarCancelas() {
  cancela1.write(map(abertura, 0, 100, CANCELA1_FECHADA, CANCELA1_ABERTA));
  cancela2.write(map(abertura, 0, 100, CANCELA2_FECHADA, CANCELA2_ABERTA));
}

// Avança um passo em direção ao alvo (0 = fechada, 100 = aberta).
// Não bloqueia: é chamada a cada volta do loop e só anda a cada 20 ms.
// Retorna true quando as cancelas chegaram ao alvo.
bool moverCancelas(int alvo) {
  if (abertura == alvo) return true;
  if (millis() - ultimoPassoCancela < INTERVALO_PASSO_CANCELA) return false;

  ultimoPassoCancela = millis();
  abertura += (alvo > abertura) ? 1 : -1;
  posicionarCancelas();
  return abertura == alvo;
}

bool cancelasEmMovimento() {
  return estado == ABRINDO_CANCELAS || estado == FECHANDO_CANCELAS;
}

// =====================================
// MELHORIA: SENSOR DE PEDESTRE + BUZZER
// Sensor proposto: PIR HC-SR501 apontado para a faixa (saída digital,
// HIGH = movimento detectado). Liga em 5V, GND e GPIO 10.
// =====================================

// Leitura tratada do sensor:
//  - filtro: só confirma presença se o sinal ficar HIGH por 200 ms seguidos
//    (descarta ruído e disparos rápidos);
//  - segurança: se ficar HIGH por mais de 20 s seguidos, o sensor é
//    considerado travado e passa a ser ignorado até voltar a LOW.
bool pedestreNaFaixa() {
  static unsigned long inicioDeteccao = 0;   // 0 = sem detecção em andamento

  if (!SENSOR_PEDESTRE_ATIVO) return false;

  if (digitalRead(SENSOR_PEDESTRE_PIN) == LOW) {
    inicioDeteccao = 0;
    return false;
  }
  if (inicioDeteccao == 0) inicioDeteccao = millis();

  unsigned long duracao = millis() - inicioDeteccao;
  return duracao >= T_CONFIRMA_PEDESTRE && duracao < T_MAX_ESPERA_PEDESTRE;
}

// Bipe intermitente (300 ms ligado / 300 ms desligado) para buzzer ATIVO.
// Usa digitalWrite de propósito: tone() ocupa o mesmo gerador de PWM (LEDC)
// dos servos e poderia atrapalhar o movimento das cancelas.
void atualizarBuzzer(bool deveTocar) {
  static unsigned long ultimaTroca = 0;
  static bool ligado = false;

  if (!deveTocar) {
    ligado = false;
  } else if (millis() - ultimaTroca >= 300) {
    ultimaTroca = millis();
    ligado = !ligado;
  }
  digitalWrite(BUZZER_PIN, ligado);
}

// =====================================
// LÓGICA DO CRUZAMENTO
// =====================================

const char* nomeEstado(Estado e) {
  switch (e) {
    case VERDE_FLUXO2:             return "Semaforo verde";
    case AMARELO_FLUXO2:           return "Semaforo amarelo";
    case VERMELHO_ANTES_CANCELAS:  return "Aguardando cancela";
    case ABRINDO_CANCELAS:         return "Abrindo cancela";
    case CANCELAS_ABERTAS:         return "Cancela aberta";
    case FECHANDO_CANCELAS:        return "Fechando cancela";
    case VERMELHO_DEPOIS_CANCELAS: return "Liberando semaforo";
  }
  return "";
}

void mudarEstado(Estado novo) {
  estado = novo;
  tempoEstado = millis();
  Serial.print("Estado: ");
  Serial.println(nomeEstado(novo));
}

void atualizarTrafego() {
  unsigned long decorrido = millis() - tempoEstado;

  switch (estado) {
    case VERDE_FLUXO2:
      setSemaforos(false, false, true);
      if (decorrido >= T_VERDE) mudarEstado(AMARELO_FLUXO2);
      break;

    case AMARELO_FLUXO2:
      setSemaforos(false, true, false);
      if (decorrido >= T_AMARELO) mudarEstado(VERMELHO_ANTES_CANCELAS);
      break;

    case VERMELHO_ANTES_CANCELAS:
      setSemaforos(true, false, false);
      // Com pedestre na faixa, as cancelas não abrem (o fluxo 1 não é liberado).
      if (decorrido >= T_TODOS_VERMELHO && !pedestreNaFaixa()) mudarEstado(ABRINDO_CANCELAS);
      break;

    case ABRINDO_CANCELAS:
      setSemaforos(true, false, false);
      if (moverCancelas(100)) mudarEstado(CANCELAS_ABERTAS);
      break;

    case CANCELAS_ABERTAS:
      setSemaforos(true, false, false);
      // Com pedestre na faixa, a cancela não desce sobre ele.
      if (decorrido >= T_CANCELAS_ABERTAS && !pedestreNaFaixa()) mudarEstado(FECHANDO_CANCELAS);
      break;

    case FECHANDO_CANCELAS:
      setSemaforos(true, false, false);
      if (moverCancelas(0)) mudarEstado(VERMELHO_DEPOIS_CANCELAS);
      break;

    case VERMELHO_DEPOIS_CANCELAS:
      setSemaforos(true, false, false);
      if (decorrido >= T_TODOS_VERMELHO) mudarEstado(VERDE_FLUXO2);
      break;
  }
}

// =====================================
// DISPLAY
// =====================================

// Redesenha a tela 1 vez por segundo (desenhar a cada volta do loop
// atrasaria os passos das cancelas). Textos sem acento: a fonte é ASCII.
void atualizarDisplay() {
  static unsigned long ultimo = 0;
  if (millis() - ultimo < 1000) return;
  ultimo = millis();

  char linha[32];
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Smart City - GS 2026");

  if (clima.valido) {
    snprintf(linha, sizeof(linha), "Temp: %.1f C", clima.temperatura);
    u8g2.drawStr(0, 28, linha);
    snprintf(linha, sizeof(linha), "Umidade: %d %%", clima.umidade);
    u8g2.drawStr(0, 40, linha);
  } else {
    u8g2.drawStr(0, 28, "Clima: aguardando...");
  }

  u8g2.drawStr(0, 58, nomeEstado(estado));
  u8g2.sendBuffer();
}

// =====================================
// OPENWEATHER
// =====================================

// Busca temperatura e umidade. Em caso de falha, mantém o último valor
// válido na tela e tenta de novo em 1 minuto.
void consultarClima() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String("https://api.openweathermap.org/data/2.5/weather?q=") + CIDADE +
               "&units=metric&appid=" + OPENWEATHER_API_KEY;
  http.begin(client, url);
  http.setTimeout(4000);

  bool sucesso = false;
  int codigo = http.GET();
  if (codigo == HTTP_CODE_OK) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok &&
        !doc["main"]["temp"].isNull()) {
      clima.temperatura = doc["main"]["temp"].as<float>();
      clima.umidade     = doc["main"]["humidity"].as<int>();
      clima.valido      = true;
      sucesso = true;
      Serial.printf("Clima: %.1f C, %d %%\n", clima.temperatura, clima.umidade);
    }
  }
  if (!sucesso) Serial.printf("OpenWeather: falha (HTTP %d)\n", codigo);

  http.end();
  intervaloClima = sucesso ? INTERVALO_CLIMA_OK : INTERVALO_CLIMA_ERRO;
}

// =====================================
// MINHA LÓGICA (chamada a cada volta do loop, sem delay)
// =====================================

void minha_logica() {
  // 1) Semáforos e cancelas
  atualizarTrafego();

  // 2) Alerta sonoro: cancela em movimento ou pedestre detectado
  atualizarBuzzer(cancelasEmMovimento() || pedestreNaFaixa());

  // 3) Clima. A consulta trava o programa por até alguns segundos,
  //    por isso nunca é feita com a cancela em movimento.
  bool horaDoClima = (intervaloClima == 0) || (millis() - ultimaConsultaClima >= intervaloClima);
  if (horaDoClima && !cancelasEmMovimento() && WiFi.status() == WL_CONNECTED) {
    ultimaConsultaClima = millis();
    consultarClima();
  }

  // 4) LCD
  atualizarDisplay();
}

// =====================================
// SETUP
// =====================================

void setup() {
  Serial.begin(115200);
  Serial.println("ESP iniciado");
  Serial.printf("Firmware do grupo - versao %d\n", versao);

  // Semáforos: tudo vermelho até a lógica assumir
  pinMode(SEM1_VERMELHO, OUTPUT);
  pinMode(SEM1_AMARELO, OUTPUT);
  pinMode(SEM1_VERDE, OUTPUT);
  pinMode(SEM2_VERMELHO, OUTPUT);
  pinMode(SEM2_AMARELO, OUTPUT);
  pinMode(SEM2_VERDE, OUTPUT);
  setSemaforos(true, false, false);

  // Buzzer e sensor de pedestre
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  if (SENSOR_PEDESTRE_ATIVO) pinMode(SENSOR_PEDESTRE_PIN, INPUT_PULLDOWN);

  // Cancelas. O servo não informa onde está, então este primeiro comando
  // (ir para "fechada") é o único movimento que não é gradual.
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  cancela1.setPeriodHertz(50);              // servo padrão: 50 Hz
  cancela2.setPeriodHertz(50);
  cancela1.attach(SERVO1_PIN, 500, 2400);   // largura de pulso em microssegundos (SG90)
  cancela2.attach(SERVO2_PIN, 500, 2400);
  abertura = 0;
  posicionarCancelas();

  // LCD ST7567: a U8g2 usa endereço de 8 bits, por isso 0x3F << 1
  u8g2.setI2CAddress(0x3F << 1);
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Smart City - GS 2026");
  u8g2.drawStr(0, 28, "Iniciando...");
  u8g2.sendBuffer();

  iniciarControleOTA();
  conectarWiFi(15000);

  tempoEstado = millis();   // o ciclo começa a contar a partir daqui
}

// =====================================
// LOOP
// =====================================

void loop() {

  // LÓGICA NORMAL DO PROGRAMA
  minha_logica();

  if (WiFi.status() == WL_CONNECTED) {
    // A CADA 10 SEGUNDOS, e só com as cancelas fechadas e paradas:
    // a atualização reinicia a placa, e um reinício com a cancela aberta
    // causaria um movimento brusco no boot.
    if (OTA_ATIVO && abertura == 0 && millis() - tempo > INTERVALO_OTA) {
      tempo = millis();
      verificarAtualizacao();
    }
  } else if (millis() - tempoReconexao > 30000) {
    // Sem rede: tenta reconectar a cada 30 s sem parar o cruzamento
    tempoReconexao = millis();
    WiFi.reconnect();
  }
}

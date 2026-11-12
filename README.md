# GS 2026 · Smart City: diagnóstico e solução

Controle de um cruzamento de maquete com **ESP32-S3**: dois semáforos, duas cancelas com servomotor, LCD com clima da API **OpenWeather** e atualização de firmware por **OTA**. Projeto da Global Solution 2026 (Edge Computing).

## Integrantes

| Nome | RM |
|---|---|
| João Victor Alves de Abreu | 564946 |
| Rodrigo Kenshin Viana Matayoshi | 564026 |

## Funcionamento

| Etapa | Semáforos | Cancelas | Duração |
|---|---|---|---|
| 1 | Verde | Abaixadas | 8 s |
| 2 | Amarelo | Abaixadas | 2 s |
| 3 | Vermelho | Abaixadas (folga) | 1 s |
| 4 | Vermelho | Sobem devagar + buzzer | ~2 s |
| 5 | Vermelho | Levantadas | 6 s |
| 6 | Vermelho | Descem devagar + buzzer | ~2 s |
| 7 | Vermelho | Abaixadas (folga) | 1 s |

Regras de segurança: o semáforo só fica verde com as cancelas abaixadas, e as cancelas só se movem com os dois semáforos no vermelho. O cruzamento funciona sem internet; só o clima e a OTA dependem de rede.

## Implementações novas

- **Buzzer de alerta (GPIO 7):** bipa enquanto a cancela se move ou há pedestre na faixa.
- **Sensor de pedestre PIR (GPIO 10):** impede a cancela de subir ou descer com alguém na faixa. Previsto no código e desativado por padrão (`SENSOR_PEDESTRE_ATIVO = false`), pois não é instalado na avaliação.
- **OTA protegida:** sem loop de reinício e sem atualizar com a cancela levantada.

Detalhes em [`3_Documentacao/GS2_Implementacoes_Novas.pdf`](3_Documentacao/GS2_Implementacoes_Novas.pdf).

## Pinagem

| Componente | GPIO |
|---|---|
| Semáforo 1: vermelho / amarelo / verde | 4 / 5 / 6 |
| Semáforo 2: vermelho / amarelo / verde | 12 / 13 / 14 |
| Servo cancela 1 / cancela 2 | 15 / 16 |
| LCD ST7567 I²C (0x3F): SDA / SCL | 8 / 9 |
| Buzzer ativo | 7 |
| Sensor PIR (previsto) | 10 |

## Estrutura

```
1_Codigo/smart_city_gs2/   código-fonte (.ino) e secrets.example.h
2_Binario_OTA/             firmware compilado (.bin) para OTA
3_Documentacao/            PDF das implementações novas
```

## Como compilar

1. Copie `secrets.example.h` para `secrets.h` (mesma pasta) e preencha a chave da OpenWeather.
2. No Arduino IDE, instale **U8g2**, **ESP32Servo** e **ArduinoJson 7.x**.
3. Placa **ESP32S3 Dev Module**: Flash 16 MB, partição *16M Flash (3MB APP/9.9MB FATFS)*, PSRAM *OPI PSRAM*.
4. **Sketch > Exportar Binário Compilado**. O arquivo para OTA é `build/esp32.esp32.esp32s3/smart_city_gs2.ino.bin` (não use o `merged.bin`).

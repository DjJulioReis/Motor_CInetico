#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include "esp_mac.h"

// --- INCLUDES SOLICITADOS ---
#include "driver/uart.h"
#include "soc/uart_struct.h"
#include "freertos/queue.h"
#include "soc/rtc_cntl_reg.h"
#include <NimBLEDevice.h>
#include "MILETO_LOGO_1.h"

#define LARGURA_TELA 128
#define ALTURA_TELA 64
#define OLED_RESET -1
Adafruit_SSD1306 display(LARGURA_TELA, ALTURA_TELA, &Wire, OLED_RESET);

// --- PINOUT ATUALIZADO PARA O CONTROLADOR CINETICO ESP32-C3 ---
#define ENC_CLK 6
#define ENC_DT   7
#define ENC_SW  10

#define SENSOR_START_PIN  21
#define SENSOR_END_PIN    1

#define STEPPER_PUL_PIN   4
#define STEPPER_DIR_PIN   9
#define STEPPER_EN_PIN    20

#define SAFETY_LOCK_PIN   2

#define DMX_UART_NUM UART_NUM_1
#define DMX_RX_PIN 20
#define DMX_TX_PIN 21
#define RS485_DIR_PIN 3  // Direcao do fluxo RS-485 para RDM

// --- CONSTANTES ---
#define MAX_SPEED         4000.0
#define MAX_ACCEL         8000.0

// DMX Channel Offsets
#define DMX_CH_MODE       1
#define DMX_CH_POS_MSB    2
#define DMX_CH_POS_LSB    3
#define DMX_CH_SPEED      4
#define DMX_CH_LOCK_CMD   5
#define DMX_CH_GROUP_ID   6
#define DMX_CH_EFFECT_ID  7
#define DMX_CH_LENGTH     7

static QueueHandle_t dmx_queue;
uint8_t raw_dmx_buf[520];
int dmx_idx = 0;
bool dmx_em_frame = false;

enum FasesMenu { FASE_MODO, FASE_CAMPO, FASE_VALOR };
FasesMenu faseAtual = FASE_MODO;

// --- VARIÁVEIS DE ESTADO DO MOTOR ---
enum HomingState { STATE_IDLE, STATE_HOMING_START, STATE_HOMING_END, STATE_CALIBRATED };
HomingState homingState = STATE_IDLE;

long currentPos = 0;
long targetPos = 0;
float currentSpeed = 0.0f;
float maxSpeed = MAX_SPEED;
float accel = MAX_ACCEL;
long startLimit = 0;
long endLimit = 10000;
unsigned long lastStepTime = 0;
unsigned long stepInterval = 0;
bool stepState = false;

// Trava Solenoide de Seguranca
bool isLocked = true;
unsigned long unlockTime = 0;
unsigned long lastActiveTime = 0;
const unsigned long UNLOCK_DELAY_MS = 150;
const unsigned long IDLE_TIMEOUT_MS = 2000;

// Kinetics Effects
bool isEffectRunning = false;
uint8_t currentEffectId = 0;
unsigned long effectStartTime = 0;
int sequenceStep = 0;
uint8_t groupId = 0;

// --- VARIÁVEIS DE ESTADO DO SISTEMA ---
bool sistemaEmModoDMX = false; // Estado Mestre
int modoAtual = 1;            // Efeito Selecionado (1 a 5)
int modoDMXTemp = 1;          // Efeito vindo da Mesa DMX

int linhaSelecionada = 1;
int enderecoDMX = 1;
int velocidad = 50;
int brilhoGeral = 255;

const char* nomesEfeitos[] = { "DMX SYSTEM", "MANUAL", "FADE", "STROBO", "SEQUENC", "FIXO", "XADREZ" };

unsigned long tempoUltimaAtividade = 0;
bool telaAcesa = true;
#define TEMPO_SLEEP_TELA 60000
unsigned long ultimoDebounce = 0;

NimBLEServer* pServer = NULL;
NimBLECharacteristic* pTxCharacteristic = NULL;
bool dispositivoConectado = false;
bool autenticado = false;
uint32_t desafioHandshake = 0;
String comandoPendente = "";
bool novoComandoBle = false;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_UUID                "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID                "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

Preferences preferences;

// Prototypes
void lockSolenoid();
void unlockSolenoid();
void lockOnIdle();
void startHoming();
void stopEffect();
void startEffect(uint8_t effectId);
void updateEffects();
void updateStepper();
void setRS485Direction(bool tx);

class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        dispositivoConectado = true;
        autenticado = false;
        randomSeed(micros());
        desafioHandshake = random(1000, 9999);
        pServer->updateConnParams(connInfo.getConnHandle(), 16, 32, 0, 400);
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        dispositivoConectado = false;
        autenticado = false;
        NimBLEDevice::startAdvertising();
    }
};

class CharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {
      String rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) { comandoPendente = rxValue; novoComandoBle = true; }
    }
};

void acordaTela() {
  tempoUltimaAtividade = millis();
  if (!telaAcesa) { display.ssd1306_command(SSD1306_DISPLAYON); telaAcesa = true; }
}

void exibirTelaSalvando() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 25);
  display.print("SALVANDO...");
  display.display();
}

void salvarConfiguracao() {
  preferences.begin("mileto_cfg", false);
  preferences.putInt("dmx_mode", sistemaEmModoDMX ? 1 : 0);
  preferences.putInt("modo", modoAtual);
  preferences.putInt("dmx", enderecoDMX);
  preferences.putInt("vel", velocidad);
  preferences.putInt("dim", brilhoGeral);
  preferences.putLong("start_lim", startLimit);
  preferences.putLong("end_lim", endLimit);
  preferences.end();
}

void carregarConfiguracao() {
  preferences.begin("mileto_cfg", true);
  sistemaEmModoDMX = (preferences.getInt("dmx_mode", 0) == 1);
  modoAtual = preferences.getInt("modo", 1);
  enderecoDMX = preferences.getInt("dmx", 1);
  velocidad = preferences.getInt("vel", 50);
  brilhoGeral = preferences.getInt("dim", 255);
  startLimit = preferences.getLong("start_lim", 0);
  endLimit = preferences.getLong("end_lim", 10000);
  preferences.end();
}

void enviarStatusBT() {
  static unsigned long last = 0;
  if (dispositivoConectado && autenticado && millis() - last >= 60) {
    last = millis();
    char buf[80];
    sprintf(buf, "STATS:%d,%d,%ld,%ld,%ld,%ld\n",
            (homingState == STATE_CALIBRATED) ? 1 : 0,
            (homingState == STATE_HOMING_START || homingState == STATE_HOMING_END) ? 1 : 0,
            currentPos, targetPos, startLimit, endLimit);
    pTxCharacteristic->setValue(buf); pTxCharacteristic->notify();
  }
}

void atualizarDisplay() {
  if (!telaAcesa) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("--- MILETO KINETIC ---");

  display.setCursor(0, 16);
  if (faseAtual == FASE_MODO) display.print("> "); else display.print("  ");

  if (sistemaEmModoDMX && faseAtual != FASE_MODO) {
      display.print("MODO: DMX SYSTEM");
  } else {
      display.print("MODO: ");
      display.print(nomesEfeitos[sistemaEmModoDMX ? 0 : modoAtual]);
  }

  display.drawFastHLine(0, 28, 128, SSD1306_WHITE);

  if (sistemaEmModoDMX) {
    display.setCursor(0, 36);
    if (faseAtual == FASE_CAMPO) display.print("> "); else display.print("  ");
    display.print("Config. Canal");
    display.setCursor(0, 50);
    if (faseAtual == FASE_VALOR) display.print("[ "); else display.print("  ");
    display.print("CANAL DMX: "); display.print(enderecoDMX);
    if (faseAtual == FASE_VALOR) display.print(" ]");
  } else {
     display.setCursor(0, 34);
     if (faseAtual == FASE_CAMPO && linhaSelecionada == 1) display.print("> "); else display.print("  ");
     display.print("Pos: "); display.print(currentPos);
     display.setCursor(0, 48);
     if (faseAtual == FASE_CAMPO && linhaSelecionada == 2) display.print("> "); else display.print("  ");
     display.print("Lim: "); display.print(startLimit); display.print("/"); display.print(endLimit);
  }
  display.display();
}

int lastClkState;
void lidarComEncoder() {
  int currentClkState = digitalRead(ENC_CLK);
  if (currentClkState != lastClkState && currentClkState == LOW) {
    acordaTela();
    bool subindo = digitalRead(ENC_DT) != currentClkState;
    if (faseAtual == FASE_MODO) {
      static int selection = sistemaEmModoDMX ? 0 : modoAtual;
      if (subindo) selection = (selection + 1) % 7;
      else selection = (selection <= 0) ? 6 : selection - 1;

      if (selection == 0) { sistemaEmModoDMX = true; }
      else { sistemaEmModoDMX = false; modoAtual = selection; }
    }
    else if (!sistemaEmModoDMX) {
      if (faseAtual == FASE_CAMPO) {
        if (subindo) { linhaSelecionada++; if (linhaSelecionada > 2) linhaSelecionada = 1; }
        else { linhaSelecionada--; if (linhaSelecionada < 1) linhaSelecionada = 2; }
      }
      else if (faseAtual == FASE_VALOR) {
         if (linhaSelecionada == 1) {
             if (subindo) { targetPos += 100; if (targetPos > endLimit) targetPos = endLimit; }
             else { targetPos -= 100; if (targetPos < startLimit) targetPos = startLimit; }
         } else if (linhaSelecionada == 2) {
             // Manual Homing trigger
             startHoming();
         }
      }
    } else {
      if (faseAtual == FASE_VALOR) {
        if (subindo) { enderecoDMX++; if (enderecoDMX > 512 - DMX_CH_LENGTH + 1) enderecoDMX = 1; }
        else { enderecoDMX--; if (enderecoDMX < 1) enderecoDMX = 512 - DMX_CH_LENGTH + 1; }
        if (dispositivoConectado && autenticado) {
           String syncMsg = "DMX:" + String(enderecoDMX) + "\n";
           pTxCharacteristic->setValue(syncMsg.c_str()); pTxCharacteristic->notify();
        }
      }
    }
    atualizarDisplay();
  }
  lastClkState = currentClkState;

  int currentSwState = digitalRead(ENC_SW);
  static int lastSwState = HIGH;
  if (currentSwState != lastSwState && currentSwState == LOW) {
    if (millis() - ultimoDebounce >= 250) {
      ultimoDebounce = millis();
      acordaTela();
      if (faseAtual == FASE_MODO) { faseAtual = FASE_CAMPO; linhaSelecionada = 1; }
      else if (faseAtual == FASE_CAMPO) { faseAtual = FASE_VALOR; }
      else if (faseAtual == FASE_VALOR) {
        exibirTelaSalvando(); salvarConfiguracao(); delay(500);
        if (dispositivoConectado && autenticado) {
          String msg = "MODO:" + String(sistemaEmModoDMX ? 0 : modoAtual) + "|DMX:" + String(enderecoDMX) + "\n";
          pTxCharacteristic->setValue(msg.c_str()); pTxCharacteristic->notify();
        }
        faseAtual = FASE_MODO; linhaSelecionada = 0;
      }
      atualizarDisplay();
    }
  }
  lastSwState = currentSwState;
}

uint32_t obterDeviceIDUnico() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint32_t deviceID = ((uint32_t)mac[2] << 24) |
                      ((uint32_t)mac[3] << 16) |
                      ((uint32_t)mac[4] << 8)  |
                      (uint32_t)mac[5];
  return deviceID;
}

String obterUIDString() {
  uint32_t dev_id = obterDeviceIDUnico();
  char buf[20];
  sprintf(buf, "4D49:%08X", dev_id);
  return String(buf);
}

void setRS485Direction(bool tx) {
  pinMode(RS485_DIR_PIN, OUTPUT);
  digitalWrite(RS485_DIR_PIN, tx ? HIGH : LOW);
}

void parseUID(String uidStr, uint8_t *man_id, uint8_t *dev_id) {
  uidStr.replace(":", "");
  uidStr.trim();
  if (uidStr.length() < 12) return;

  man_id[0] = strtol(uidStr.substring(0, 2).c_str(), NULL, 16);
  man_id[1] = strtol(uidStr.substring(2, 4).c_str(), NULL, 16);

  dev_id[0] = strtol(uidStr.substring(4, 6).c_str(), NULL, 16);
  dev_id[1] = strtol(uidStr.substring(6, 8).c_str(), NULL, 16);
  dev_id[2] = strtol(uidStr.substring(8, 10).c_str(), NULL, 16);
  dev_id[3] = strtol(uidStr.substring(10, 12).c_str(), NULL, 16);
}

void enviarRdmSetDmxAddress(String uidStr, int novoCanal) {
  uint8_t dest_man[2] = {0, 0};
  uint8_t dest_dev[4] = {0, 0, 0, 0};
  parseUID(uidStr, dest_man, dest_dev);

  uint8_t rdm_packet[28];
  rdm_packet[0] = 0xCC;
  rdm_packet[1] = 0x01;
  rdm_packet[2] = 26;

  rdm_packet[3] = dest_man[0];
  rdm_packet[4] = dest_man[1];
  rdm_packet[5] = dest_dev[0];
  rdm_packet[6] = dest_dev[1];
  rdm_packet[7] = dest_dev[2];
  rdm_packet[8] = dest_dev[3];

  rdm_packet[9] = 0x4D;
  rdm_packet[10] = 0x49;
  rdm_packet[11] = 0x00;
  rdm_packet[12] = 0x00;
  rdm_packet[13] = 0x00;
  rdm_packet[14] = 0x01;

  static uint8_t transaction_num = 0;
  rdm_packet[15] = transaction_num++;
  rdm_packet[16] = 0x01;
  rdm_packet[17] = 0x00;
  rdm_packet[18] = 0x00;
  rdm_packet[19] = 0x00;

  rdm_packet[20] = 0x30;
  rdm_packet[21] = 0x00;
  rdm_packet[22] = 0xF0;
  rdm_packet[23] = 0x02;

  rdm_packet[24] = (novoCanal >> 8) & 0xFF;
  rdm_packet[25] = novoCanal & 0xFF;

  uint16_t checksum = 0;
  for (int i = 0; i < 26; i++) {
    checksum += rdm_packet[i];
  }

  rdm_packet[26] = (checksum >> 8) & 0xFF;
  rdm_packet[27] = checksum & 0xFF;

  setRS485Direction(true);

  uart_set_line_inverse(DMX_UART_NUM, UART_SIGNAL_TXD_INV);
  delayMicroseconds(180);

  uart_set_line_inverse(DMX_UART_NUM, 0);
  delayMicroseconds(20);

  uart_write_bytes(DMX_UART_NUM, (const char*)rdm_packet, 28);

  setRS485Direction(false);
}

void executarVarreduraRDM() {
  if (!dispositivoConectado || !autenticado) return;

  pTxCharacteristic->setValue("RDM_START\n");
  pTxCharacteristic->notify();
  delay(300);

  uint32_t dev_id = obterDeviceIDUnico();
  char buf[45];
  sprintf(buf, "RDM_DEV:4d49,%08X,%d,7,MILETO_KINETIC_MOTOR\n", dev_id, enderecoDMX);
  pTxCharacteristic->setValue(buf);
  pTxCharacteristic->notify();
  delay(300);

  pTxCharacteristic->setValue("RDM_END\n");
  pTxCharacteristic->notify();
  delay(100);
}

void processarBluetooth() {
  acordaTela();
  comandoPendente.replace("\n", ""); comandoPendente.replace("\r", ""); comandoPendente.trim();
  int div = comandoPendente.indexOf(':'); if (div == -1) return;
  String cmd = comandoPendente.substring(0, div); String val = comandoPendente.substring(div + 1);
  int iv = val.toInt();

  if (cmd == "AUTH_RESPONSE") {
    if (iv == (desafioHandshake * 2) + 7) {
      autenticado = true;
      pTxCharacteristic->setValue("MILETO_AUTH:VALID\nCONNECTED_OK\n"); pTxCharacteristic->notify();
      delay(500);
      executarVarreduraRDM();
    } else {
      autenticado = false;
      pTxCharacteristic->setValue("MILETO_AUTH:INVALID\n"); pTxCharacteristic->notify();
    }
    return;
  }
  if (!autenticado) return;

  // --- PARSE DOS COMANDOS EXCLUSIVOS DO MOTOR CINETICO ---
  if (cmd == "SET_POS") {
    targetPos = val.toInt();
  }
  else if (cmd == "SET_POINT_A") {
    startLimit = currentPos;
    salvarConfiguracao();
  }
  else if (cmd == "SET_POINT_B") {
    endLimit = currentPos;
    homingState = STATE_CALIBRATED;
    salvarConfiguracao();
  }
  else if (cmd == "CALIBRAR") {
    startHoming();
  }
  else if (cmd == "PARAR") {
    targetPos = currentPos;
    stopEffect();
  }
  else if (cmd == "SET_MODO") {
    if (iv == 0) { sistemaEmModoDMX = true; }
    else { sistemaEmModoDMX = false; modoAtual = iv; }
  }
  else if (cmd == "SET_VEL") { velocidad = min(iv, 100); maxSpeed = map(velocidad, 0, 100, 100, MAX_SPEED); }
  else if (cmd == "SET_DMX") {
    int commaIdx = val.indexOf(',');
    if (commaIdx != -1) {
      String uid = val.substring(0, commaIdx);
      int canal = val.substring(commaIdx + 1).toInt();
      enviarRdmSetDmxAddress(uid, canal);

      String testUid = uid;
      testUid.toUpperCase();
      testUid.replace(":", "");

      String localUid = obterUIDString();
      localUid.toUpperCase();
      localUid.replace(":", "");

      if (testUid.indexOf(localUid) != -1) {
        enderecoDMX = canal;
        exibirTelaSalvando();
        salvarConfiguracao();
        delay(500);
      }
    } else {
      enderecoDMX = iv;
    }
  }
  else if (cmd == "VARREDURA_RDM") { executarVarreduraRDM(); }
  else if (cmd == "CHAVE_MODO") { sistemaEmModoDMX = (val == "DMX"); if(!sistemaEmModoDMX && modoAtual == 0) modoAtual = 1; }
  else if (cmd == "GRAVAR") { exibirTelaSalvando(); salvarConfiguracao(); pTxCharacteristic->setValue("GRAVAR:OK\n"); pTxCharacteristic->notify(); delay(1000); }
  atualizarDisplay();
}

void lockSolenoid() {
    digitalWrite(SAFETY_LOCK_PIN, LOW);
    isLocked = true;
}

void unlockSolenoid() {
    if (isLocked) {
        digitalWrite(SAFETY_LOCK_PIN, HIGH);
        unlockTime = millis();
        isLocked = false;
    }
    lastActiveTime = millis();
}

void lockOnIdle() {
    if (!isLocked) {
        if (millis() - lastActiveTime >= IDLE_TIMEOUT_MS) {
            lockSolenoid();
        }
    }
}

void startHoming() {
    homingState = STATE_HOMING_START;
    targetPos = -999999;
    maxSpeed = 800.0f;
}

void stopEffect() { isEffectRunning = false; }

void startEffect(uint8_t effectId) {
    isEffectRunning = true;
    currentEffectId = effectId;
    effectStartTime = millis();
    sequenceStep = 0;
}

void updateEffects() {
    if (!isEffectRunning) return;
    unsigned long elapsed = millis() - effectStartTime;

    switch (currentEffectId) {
        case 1: // Loop Continuo A -> B -> A
            if (currentPos == targetPos) {
                if (sequenceStep == 0) { targetPos = endLimit; sequenceStep = 1; }
                else { targetPos = startLimit; sequenceStep = 0; }
            }
            break;
        case 2: // Oscilacao Senoidal Suave
            {
                float angle = (2.0f * PI * (float)(elapsed % 3000)) / 3000.0f;
                float norm = (sinf(angle) + 1.0f) / 2.0f;
                targetPos = startLimit + (long)(norm * (float)(endLimit - startLimit));
            }
            break;
        case 3: // Onda em Cascata com Atraso de Fase de Grupo
            {
                float delayVal = (float)groupId * 0.5f;
                float angle = (2.0f * PI * ((float)elapsed / 4000.0f)) + delayVal;
                float norm = (sinf(angle) + 1.0f) / 2.0f;
                targetPos = startLimit + (long)(norm * (float)(endLimit - startLimit));
            }
            break;
    }
}

void updateStepper() {
    if (currentPos != targetPos) {
        if (isLocked) { unlockSolenoid(); return; }
        if (millis() - unlockTime < UNLOCK_DELAY_MS) return;
    } else {
        lockOnIdle();
        currentSpeed = 0;
        return;
    }

    if (homingState == STATE_HOMING_START && digitalRead(SENSOR_START_PIN) == LOW) {
        currentPos = 0;
        startLimit = 0;
        homingState = STATE_HOMING_END;
        targetPos = 999999;
        maxSpeed = 800.0f;
        return;
    } else if (homingState == STATE_HOMING_END && digitalRead(SENSOR_END_PIN) == LOW) {
        endLimit = currentPos;
        targetPos = currentPos;
        homingState = STATE_CALIBRATED;
        maxSpeed = MAX_SPEED;
        return;
    }

    unsigned long now = micros();
    unsigned long timeDelta = now - lastStepTime;

    if (stepInterval == 0 || timeDelta >= stepInterval) {
        long distanceToGo = targetPos - currentPos;
        float dt = (timeDelta > 0 && timeDelta < 100000) ? (timeDelta / 1000000.0f) : 0.001f;

        if (distanceToGo > 0) {
            digitalWrite(STEPPER_DIR_PIN, HIGH);
            currentSpeed += (accel * dt);
            if (currentSpeed > maxSpeed) currentSpeed = maxSpeed;
            if (currentSpeed < 50.0f) currentSpeed = 50.0f;

            float decelDistance = (currentSpeed * currentSpeed) / (2.0f * accel);
            if (distanceToGo <= decelDistance) {
                currentSpeed -= (accel * dt);
                if (currentSpeed < 50.0f) currentSpeed = 50.0f;
            }
            currentPos++;
        } else if (distanceToGo < 0) {
            digitalWrite(STEPPER_DIR_PIN, LOW);
            currentSpeed -= (accel * dt);
            if (fabsf(currentSpeed) > maxSpeed) currentSpeed = -maxSpeed;
            if (fabsf(currentSpeed) < 50.0f) currentSpeed = -50.0f;

            float decelDistance = (currentSpeed * currentSpeed) / (2.0f * accel);
            if (abs(distanceToGo) <= decelDistance) {
                currentSpeed += (accel * dt);
                if (currentSpeed > -50.0f) currentSpeed = -50.0f;
            }
            currentPos--;
        }

        stepState = !stepState;
        digitalWrite(STEPPER_PUL_PIN, stepState ? HIGH : LOW);
        lastStepTime = now;

        if (fabsf(currentSpeed) < 1.0f) stepInterval = 0;
        else stepInterval = (unsigned long)(1000000.0f / fabsf(currentSpeed));
    }
}

void processarDMX() {
  uart_event_t evt;
  while (xQueueReceive(dmx_queue, (void*)&evt, 0)) {
    if (evt.type == UART_BREAK) { uart_flush_input(DMX_UART_NUM); dmx_idx = 0; dmx_em_frame = true; }
    else if (evt.type == UART_DATA && dmx_em_frame) {
      size_t l = 0; uart_get_buffered_data_len(DMX_UART_NUM, &l);
      if (l > 0) {
        uint8_t t[64]; int r = uart_read_bytes(DMX_UART_NUM, t, (l > 64) ? 64 : l, 0);
        for (int i = 0; i < r; i++) {
          if (dmx_em_frame) {
            if (dmx_idx < 520) raw_dmx_buf[dmx_idx] = t[i]; dmx_idx++;
            if (dmx_idx >= (enderecoDMX + DMX_CH_LENGTH)) {
              if (raw_dmx_buf[0] == 0x00) {
                int idx = enderecoDMX;

                uint8_t mode = raw_dmx_buf[idx];
                uint16_t rawPos = (raw_dmx_buf[idx + DMX_CH_POS_MSB - 1] << 8) | raw_dmx_buf[idx + DMX_CH_POS_LSB - 1];
                uint8_t speedVal = raw_dmx_buf[idx + DMX_CH_SPEED - 1];
                uint8_t lockCmd = raw_dmx_buf[idx + DMX_CH_LOCK_CMD - 1];
                uint8_t netGroupId = raw_dmx_buf[idx + DMX_CH_GROUP_ID - 1];
                uint8_t effectId = raw_dmx_buf[idx + DMX_CH_EFFECT_ID - 1];

                maxSpeed = map(speedVal, 0, 255, 100, MAX_SPEED);
                groupId = netGroupId;

                if (lockCmd >= 128) lockSolenoid();

                switch (mode) {
                  case 0: stopEffect(); targetPos = currentPos; break;
                  case 1: {
                    stopEffect();
                    targetPos = map(rawPos, 0, 65535, startLimit, endLimit);
                    break;
                  }
                  case 2: startEffect(effectId); break;
                  case 3: stopEffect(); startHoming(); break;
                }
              }
              dmx_em_frame = false;
            }
          }
        }
      }
    }
  }
}

void desenharLogo(const unsigned char* bitmap, int largura, int altura) {
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, largura, altura, WHITE);
  display.display();
}

void setup() {
  Serial.begin(115200);
  Serial.println("MILETO KINETIC STARTING...");
  tempoUltimaAtividade = millis();

  pinMode(RS485_DIR_PIN, OUTPUT);
  digitalWrite(RS485_DIR_PIN, LOW);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastClkState = digitalRead(ENC_CLK);

  pinMode(STEPPER_PUL_PIN, OUTPUT);
  pinMode(STEPPER_DIR_PIN, OUTPUT);
  pinMode(STEPPER_EN_PIN, OUTPUT);
  digitalWrite(STEPPER_EN_PIN, LOW);

  pinMode(SENSOR_START_PIN, INPUT_PULLUP);
  pinMode(SENSOR_END_PIN, INPUT_PULLUP);
  pinMode(SAFETY_LOCK_PIN, OUTPUT);
  lockSolenoid();

  Wire.begin(8, 9);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { Serial.println("OLED ERR"); }
  display.setTextColor(SSD1306_WHITE);
  desenharLogo(MILETO_LOGO_1, LARGURA_TELA, ALTURA_TELA);
  delay(3000);

  carregarConfiguracao();
  atualizarDisplay();

  NimBLEDevice::init("MILETO");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic *pRxCharacteristic = pService->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE);
  pRxCharacteristic->setCallbacks(new CharacteristicCallbacks());
  pService->start();

  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  BLEAdvertisementData mainAdv;
  mainAdv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  mainAdv.setCompleteServices(BLEUUID(SERVICE_UUID));
  mainAdv.setName("MILETO");
  pAdvertising->setAdvertisementData(mainAdv);

  BLEAdvertisementData scanResponseData;
  scanResponseData.setName("MILETO");
  pAdvertising->setScanResponseData(scanResponseData);
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->enableScanResponse(true);
  pAdvertising->setMinInterval(32);
  pAdvertising->setMaxInterval(64);
  pAdvertising->start();

  uart_config_t uart_cfg = { .baud_rate = 250000, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_2, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
  uart_param_config(DMX_UART_NUM, &uart_cfg);
  uart_set_pin(DMX_UART_NUM, UART_PIN_NO_CHANGE, DMX_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(DMX_UART_NUM, 1024, 0, 20, &dmx_queue, 0);

  Serial.println("MILETO KINETIC READY!");
}

void loop() {
  if (novoComandoBle) { processarBluetooth(); novoComandoBle = false; comandoPendente = ""; }
  static bool ultimoEstadoConexao = false;
  static unsigned long lastAuthReq = 0;
  if (dispositivoConectado != ultimoEstadoConexao) {
    ultimoEstadoConexao = dispositivoConectado;
    if (dispositivoConectado) { lastAuthReq = millis(); acordaTela(); }
    atualizarDisplay();
  }
  if (dispositivoConectado && !autenticado && millis() - lastAuthReq >= 2000) {
    lastAuthReq = millis();
    String msg = "AUTH_CHALLENGE:"; msg += desafioHandshake; msg += "\n";
    pTxCharacteristic->setValue(msg.c_str()); pTxCharacteristic->notify();
  }
  if (telaAcesa && (millis() - tempoUltimaAtividade >= TEMPO_SLEEP_TELA)) {
    display.clearDisplay(); display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    telaAcesa = false;
  }

  lidarComEncoder();
  updateStepper();
  updateEffects();

  if (sistemaEmModoDMX) { processarDMX(); }
  enviarStatusBT();
}

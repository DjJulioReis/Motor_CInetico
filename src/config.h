#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- Configuração de Pinos ESP32-C3 ---
// Observação: O ESP32-C3 possui GPIOs limitadas (GPIO 0-10, 18-21). Vamos alocar com muito cuidado.
// Evitamos GPIO 18/19 que são USB D-/D+ nativos para prevenir travamento no boot ou quebra do monitor serial.

// Display OLED (I2C)
#define OLED_SDA          5
#define OLED_SCL          6
#define OLED_RESET       -1 // Sem Reset físico
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT    64

// Encoder Rotativo Local do Menu
#define ENCODER_CLK       0
#define ENCODER_DT        1
#define ENCODER_SW        10 // GPIO 10 usado para chave do encoder

// DMX / RDM (UART1)
#define DMX_RX_PIN        20
#define DMX_TX_PIN        21
#define DMX_DE_RE_PIN     3  // RS-485 Direção (HIGH = Transmitir, LOW = Receber)

// Driver de Motor de Passo (NEMA 34)
#define STEPPER_PUL_PIN   4
#define STEPPER_DIR_PIN   9
#define STEPPER_EN_PIN    20

// Encoder de Feedback Acoplado ao Motor (Malha Fechada)
// Usamos interrupções de hardware nas GPIOs 18 e 19 para leitura de quadratura ultra-rápida das fases A e B do encoder do motor.
#define MOTOR_ENC_A       18
#define MOTOR_ENC_B       19
#define MOTOR_ENC_PPR     1000 // Resolução do encoder (Pulsos por Volta)

// Sensores Fotoelétricos Difusos de Fim de Curso
#define SENSOR_START_PIN  21
#define SENSOR_END_PIN    1

// Trava de Segurança Solenoide TAU-S0837DL
#define SAFETY_LOCK_PIN   2

// --- Constantes Cinemáticas Mecânicas ---
#define STEPS_PER_REV     1600.0f
#define MOTOR_SHAFT_DIA    15.0f
#define PULLEY_UPPER_DIA   70.0f
#define CABLE_DRUM_DIA     170.0f

#define KINEMATIC_RATIO    (PULLEY_UPPER_DIA / MOTOR_SHAFT_DIA)
#define PI_VAL             3.1415926535f
#define STEPS_PER_MM       ((STEPS_PER_REV * KINEMATIC_RATIO) / (PI_VAL * CABLE_DRUM_DIA))

#define MAX_SPEED         4000.0 // passos/seg
#define MAX_ACCEL         8000.0 // passos/seg^2

// Mapa de Canais DMX (Padrão Mileto)
#define DMX_CH_MODE       1  // 0: Parado, 1: Ir para Posição, 2: Efeito, 3: Calibrar/Homing
#define DMX_CH_POS_MSB    2
#define DMX_CH_POS_LSB    3
#define DMX_CH_SPEED      4
#define DMX_CH_LOCK_CMD   5
#define DMX_CH_GROUP_ID   6
#define DMX_CH_EFFECT_ID  7
#define DMX_CH_LENGTH     7

// UUIDs Bluetooth Low Energy (BLE) App Mileto
#define BLE_SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_CTRL_UUID         "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_STATUS_UUID       "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

#endif // CONFIG_H

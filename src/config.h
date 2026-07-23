#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- ESP32-C3 Pin Configuration ---
// Note: ESP32-C3 has limited GPIOs (GPIO 0-10, 18-21). Let's allocate them carefully.
// To avoid conflicts with Native USB CDC on GPIO 18/19, we avoid using 18 & 19 for active GPIOs.

// OLED Display (I2C)
#define OLED_SDA          5
#define OLED_SCL          6
#define OLED_RESET       -1 // None
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT    64

// Rotary Encoder with Switch
#define ENCODER_CLK       0
#define ENCODER_DT        1
#define ENCODER_SW        2

// DMX / RDM (UART1)
#define DMX_RX_PIN        7
#define DMX_TX_PIN        10
#define DMX_DE_RE_PIN     3  // Transmit Enable (Active High) / Receive Enable (Active Low)

// Stepper Motor (NEMA 34 Microstep Driver)
#define STEPPER_PUL_PIN   4
#define STEPPER_DIR_PIN   9
#define STEPPER_EN_PIN    20

// Diffuse Photoelectric Positioning Sensors
#define SENSOR_START_PIN  21
#define SENSOR_END_PIN    1

// TAU-S0837DL Safety Solenoid Lock (Active High via Transistor/Relay)
#define SAFETY_LOCK_PIN   2

// --- Kinematics & Mechanical Constants ---
#define STEPS_PER_REV     1600   // Microstepping configured on NEMA 34 Driver

// Physical diameters (in mm)
#define MOTOR_SHAFT_DIA    15.0  // Motor shaft diameter
#define PULLEY_UPPER_DIA   70.0  // Upper pulley diameter
#define CABLE_DRUM_DIA     170.0 // Cable drum diameter (where cord wraps around)

// Gear Reduction Ratio calculations:
// Motor Shaft wraps to Upper Pulley (Ratio: 70 / 15)
// Cable Drum is on the same shaft as Upper Pulley (Ratio: 1 / 1)
#define KINEMATIC_RATIO    (PULLEY_UPPER_DIA / MOTOR_SHAFT_DIA)

// Steps per millimeter of linear cable travel:
// 1 motor revolution = PI * MOTOR_SHAFT_DIA * (PULLEY_UPPER_DIA / MOTOR_SHAFT_DIA) is not used.
// Direct relationship: Motor drives the Upper Pulley, so one full Upper Pulley rotation needs:
// STEPS_FOR_ONE_PULLEY_ROTATION = STEPS_PER_REV * (PULLEY_UPPER_DIA / MOTOR_SHAFT_DIA)
// Since Upper Pulley shares the same shaft as the Cable Drum, one drum rotation is:
// DRUM_CIRCUMFERENCE = PI * CABLE_DRUM_DIA
// Hence: Steps per mm of Cable Travel = STEPS_FOR_ONE_PULLEY_ROTATION / (PI * CABLE_DRUM_DIA)
#define PI_VAL             3.1415926535f
#define STEPS_PER_MM       ((STEPS_PER_REV * KINEMATIC_RATIO) / (PI_VAL * CABLE_DRUM_DIA))

// Target positioning limits (in mm)
#define CALIBRATED_CABLE_TRAVEL_MM  1200.0 // 1.2 meters total vertical travel limit

// Positioning disc has 50 teeth / slots for optical encoder
#define SENSOR_DISC_TEETH  50
#define STEPS_PER_TOOTH    ((STEPS_PER_REV * KINEMATIC_RATIO) / SENSOR_DISC_TEETH)

#define MAX_SPEED         4000.0 // steps/sec
#define MAX_ACCEL         8000.0 // steps/sec^2

// DMX Channel Map (Standard Moving/Positioning Fixture)
#define DMX_CH_MODE       1  // 0: Idle/Stop, 1: Goto Position, 2: Run Sequence, 3: Calibrate
#define DMX_CH_POS_MSB    2  // Target position High Byte
#define DMX_CH_POS_LSB    3  // Target position Low Byte
#define DMX_CH_SPEED      4  // Target speed (0-255 map to 0-MAX_SPEED)
#define DMX_CH_LOCK_CMD   5  // 0-127: Auto Lock, 128-255: Force Lock ON
#define DMX_CH_GROUP_ID   6  // Sync groups (0-255)
#define DMX_CH_EFFECT_ID  7  // Effect sequence selection
#define DMX_CH_LENGTH     7  // 7-channel fixture

// App Configuration
#define BLE_SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_CTRL_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_STATUS_UUID       "c7e462d0-eb14-41d3-a9d0-0870932258aa"

#endif // CONFIG_H

# Sensor-Driven Adaptive Sanitation Protocol

## Overview

The Sensor-Driven Adaptive Sanitation Protocol is an IoT-based hygiene monitoring system built on the NodeMCU ESP32-S platform. It uses gas and IR sensors to detect sanitation issues in public restrooms in real time. The system automates exhaust fan control, provides staff alerts through a buzzer and LCD display, and includes keypad-based authentication for cleaning staff acknowledgment. All sensor data is processed locally and can be transmitted to a cloud dashboard for centralized monitoring.

The system ensures continuous environmental monitoring of air quality, floor conditions, and occupancy while enforcing staff accountability through secure PIN verification. This provides a reliable, automated solution for maintaining hygiene standards in high-traffic public facilities.

## Problem Statement

Maintaining consistent hygiene in public washrooms continues to be a major challenge, especially in high-footfall locations such as educational institutions, transportation hubs, railway stations, bus terminals, and commercial complexes. Traditional sanitation practices depend entirely on manual inspections and fixed cleaning schedules, which frequently fail to address issues in real time. As a result, problems such as unpleasant odors from accumulated gases, wet and slippery floors, and undetected occupancy often remain unnoticed for extended periods. These conditions not only degrade user experience but also increase the risk of infectious disease transmission and compromise public safety. Additionally, the lack of verifiable mechanisms to confirm that cleaning staff have completed their duties leads to inconsistent maintenance and reduced accountability.

The Sensor-Driven Adaptive Sanitation Protocol directly addresses these gaps by delivering automated, sensor-based monitoring and staff verification specifically tailored for public washrooms.

## Project Abstract

The Sensor Driven Adaptive Sanitation Protocol (SDASP) is an intelligent IoT-based monitoring system designed to address the persistent problem of poor hygiene maintenance in public restrooms. The system employs a network of environmental and occupancy sensors to continuously monitor cleanliness levels and detect hygiene violations in real time. The central component is the NodeMCU ESP32-S microcontroller, which processes data from the Fermion MEMS Gas Detection Sensors (H₂S and NH₃) and an IR sensor. Sensor readings are compared against calibrated thresholds, triggering alerts through a buzzer, LCD display, and relay-controlled exhaust fan. A keypad-based staff verification module ensures adherence to cleaning schedules, while data is transmitted to a cloud dashboard for real-time monitoring.

## Block Diagram

![Smart Toilet Hygiene Monitoring System - Block Diagram](https://github.com/Awad235/Sensor-Driven-Adaptive-Sanitation-Protocol/blob/main/diagram/SDASP%20block%20diagram.png)

## ESP32 Pin Assignment

![ESP32 Pin Assignment - Project Wiring Reference](https://github.com/Awad235/Sensor-Driven-Adaptive-Sanitation-Protocol/blob/main/diagram/Esp32%20Pin%20assignment.jpeg)

*Detailed pin mapping for all sensors, keypad, relay, buzzer, and LCD connections used in the project.*

*Complete hardware schematic showing power supply, ESP32 connections, sensor interfacing, and peripheral modules.*

## System Architecture

The architecture is structured into three primary functional layers:

- **Sensor Layer (Data Acquisition)**: Responsible for acquiring real-time environmental and operational data. It includes Fermion MEMS Gas Detection Sensor (H₂S), Fermion MEMS Gas Detection Sensor (NH₃), FC-51 IR sensor (occupancy detection), and a 4×3 matrix keypad for staff PIN input.

- **Processing and Decision Layer**: Powered by the NodeMCU ESP32-S microcontroller. It reads analog and digital sensor inputs via its 12-bit ADC and GPIO pins, processes the data, compares values against predefined thresholds, and executes decision logic for alerts and control actions.

- **Alert and Communication Layer**: Handles output and response mechanisms. It activates a piezoelectric buzzer for audible alerts, a 16×2 LCD display for visual status and messages, and a relay module to control the exhaust fan. The ESP32’s built-in Wi-Fi enables optional cloud logging and remote dashboard access.

## Operational Workflow

The system follows a clear sequential process:

1. Sensors continuously monitor environmental and occupancy parameters.
2. Data is transmitted to the ESP32 microcontroller for processing.
3. The system evaluates the data against predefined hygiene thresholds.
4. If a violation is detected, alerts are triggered via buzzer and LCD.
5. The relay module activates ventilation systems if required.
6. Cleaning staff acknowledge and respond using the keypad.
7. All events are timestamped and logged for traceability and cloud transmission.

## System Components

The project integrates the following core components:

- **NodeMCU ESP32-S** — Main microcontroller (all phases)  
- **Fermion MEMS Gas Detection Sensor (H₂S)** — Hydrogen Sulfide detection (Phase 4)  
- **Fermion MEMS Gas Detection Sensor (NH₃)** — Ammonia detection (Phase 4)  
- **DHT22 Sensor** — Temperature and humidity monitoring (Phase 4)  
- **IR Sensor (FC-51)** — Occupancy detection (Phase 3)  
- **16×2 LCD Display (I²C)** — System status and alerts (Phases 2–6)  
- **4×3 Matrix Keypad** — Staff PIN verification (Phase 6)  
- **Relay Module** — Exhaust fan and appliance control (Phase 5)  
- **Buzzer** — Audible alerts (Phase 5)  

## Development Phases

The system is implemented in seven progressive phases:

| Phase | Description                          |
|-------|--------------------------------------|
| 1     | Environment Setup                    |
| 2     | LCD Integration                      |
| 3     | IR Occupancy Detection               |
| 4     | Sensor Suite Integration             |
| 5     | Alert & Control Mechanisms           |
| 6     | PIN Keypad Authentication            |
| 7     | Cloud/Dashboard Integration          |

## Bill of Materials

| S. No | Component                          | Part No. / Model                          | Phase      | Qty | Unit Cost | Total Cost |
|-------|------------------------------------|-------------------------------------------|------------|-----|-----------|------------|
| 1     | NodeMCU ESP32-S                    | NodeMCU ESP32-S (38-pin)                  | All        | 1   | ₹455      | ₹455       |
| 2     | IR Sensor                          | FC-51 / TCRT5000                          | Phase 3    | 1   | ₹29       | ₹29        |
| 3     | Gas Sensor H₂S                     | Fermion MEMS Gas Detection Sensor (H₂S)   | Phase 4    | 1   | ₹988      | ₹988       |
| 4     | Gas Sensor NH₃                     | Fermion MEMS Gas Detection Sensor (NH₃)   | Phase 4    | 1   | ₹875      | ₹875       |
| 5     | 16×2 LCD Display                   | JHD162A + I²C Backpack                    | Phases 2–6 | 1   | ₹150      | ₹150       |
| 6     | 4×3 Matrix Keypad                  | Generic Membrane 4×3                      | Phase 6    | 1   | ₹39       | ₹39        |
| 7     | Relay Module                       | SRD-05VDC-SL-C (1ch)                      | Phase 5    | 1   | ₹39       | ₹39        |
| 8     | Buzzer                             | Active Piezo Buzzer 5V                    | Phase 5    | 1   | ₹30       | ₹30        |
| 9     | DHT22 Sensor                       | DHT22 / AM2302                            | Phase 4    | 1   | ₹112      | ₹112       |
|       | **GRAND TOTAL**                    |                                           |            |     |           | **₹2,717** |

## Team Roles

| Role                                | Team Members                                      |
|-------------------------------------|---------------------------------------------------|
| Documentation                       | Sushakitha, Sanjana, Neha                         |
| Software Development                | Tejashwini, Keerthana                             |
| Hardware Implementation & Testing   | Raphael, Maaz, Awad, Murgesh, Manoj, Anush        |
| Hardware & Circuit Design           | Shrinath, Vidya Sagar, Anush                      |

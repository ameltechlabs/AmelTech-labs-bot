/*
 * host_stub.cpp
 * ---------------------------------------------------------------------------
 * Definitions for the globals the Arduino and WiFi host stubs declare.
 * Link this alongside the library sources when building the host tests or
 * compile checking the example sketches on a desktop.
 * ---------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <WiFi.h>

HardwareSerial Serial;
HostWiFiClass WiFi;

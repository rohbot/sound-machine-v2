#pragma once
// Copy secrets.example.h to secrets.h to join your existing Wi-Fi network.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD "quietnight"
#endif
// Waveshare's factory demo uses RGB order. Use NEO_GRB if your revision swaps red/green.
#define RING_PIXEL_ORDER NEO_RGB

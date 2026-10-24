// Copy this file to secrets.h and fill in your real values.
// secrets.h is git-ignored. Never share or commit it.
#pragma once

#define WIFI_SSID   "AVIM_KADAM"
#define WIFI_PASS   "KADAM@4184"

// Laptop address (its fixed IP), port 8000. Used for the to-do / timer polling.
#define SERVER_URL  "http://192.168.0.200:8000/api/state"

// Must be EXACTLY the same text as NOVA_TOKEN in the laptop's .env file (16+ characters).
// The laptop sends it with every display command; the ESP32 rejects anything without it.
#define DEVICE_TOKEN "CHANGE_ME"

// Fixed address for the ESP32, so the laptop always knows where to find it.
// Pick one OUTSIDE the router's DHCP pool (your laptop uses .200, so .201 is fine).
// Set USE_STATIC_IP to 0 to let the router choose instead.
#define USE_STATIC_IP 1
#define STATIC_IP     192,168,0,201
#define GATEWAY_IP    192,168,0,1
#define SUBNET_MASK   255,255,255,0
#define DNS_IP        8,8,8,8

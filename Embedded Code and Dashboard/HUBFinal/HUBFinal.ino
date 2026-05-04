#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include "dw3000.h"
#include <Preferences.h>

#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  5

// Buzzer on GPIO 32 — HIGH = beeping (tag outside room)
#define BUZZER_PIN 32

// Room bounding handled dynamically now
#define ROOM_X_MAX 7.85
#define ROOM_Y_MAX 10.5

#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define ALL_MSG_SN_IDX 2
#define ALL_MSG_COMMON_LEN 10
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define POLL_TX_TO_RESP_RX_DLY_UUS 300
#define RESP_RX_TIMEOUT_UUS 3000


extern dwt_txconfig_t txconfig_options;
extern SPISettings _fastSPI;

// -------------------------------------------------------
// MULTI-ROOM CONFIGURATION
// -------------------------------------------------------
struct RoomConfig {
    const char* name;
    double max_x;
    double max_y;
    double a1_z;
    double a2_z;
    double hub_z;
    double a1_x;
    double a1_y;
    double a2_x;
    double a2_y;
    double hub_x;
    double hub_y;
};

const RoomConfig rooms[] = {
    // Profile 1: C201-6B
    {"C201-6B",       7.85, 10.50, 2.20, 2.35, 2.20,  0.00, 0.00,  7.85, 0.00,  0.00, 9.50},
    // Profile 2: C201-3n6 (Canvas 10x12)
    {"C201-3n6",      10.00, 12.00, 0.75, 2.35, 2.20, 8.00, 8.00,  7.85, 0.00,  0.00, 10.50},
    // Profile 3: C201-2n3n5n6 (Canvas 10x12)
    {"C201-2n3n5n6",  21.70, 15.62, 0.75, 2.35, 2.20, 8.00, 11.70,  7.85, 0.00,  0.00, 10.50},
    // Profile 4: LHC-2FTut
    {"LHC-2FTut",     7.69, 10.94, 1.83, 1.83, 1.83,  0.00, 9.94,  7.69, 9.94,  3.85, 0.00}
};
const int NUM_ROOMS = 4;
int currentRoomIndex = 0;
Preferences preferences;

// Button debounce for room switching
#define BOOT_BUTTON_PIN 0
unsigned long lastButtonPress = 0;

void printRoomConfig() {
    RoomConfig r = rooms[currentRoomIndex];
    Serial.printf("[ROOM_CONFIG] {\"name\":\"%s\",\"x_max\":%.2f,\"y_max\":%.2f,\"a1_z\":%.2f,\"a2_z\":%.2f,\"hub_z\":%.2f,\"hub_x\":%.2f,\"hub_y\":%.2f,\"a1_x\":%.2f,\"a1_y\":%.2f,\"a2_x\":%.2f,\"a2_y\":%.2f}\n",
                  r.name, r.max_x, r.max_y, r.a1_z, r.a2_z, r.hub_z, r.hub_x, r.hub_y, r.a1_x, r.a1_y, r.a2_x, r.a2_y);
}

double d1 = 0, d2 = 0, d3 = 0;
unsigned long lastD1 = 0, lastD2 = 0, lastD3 = 0;

// Buzzer debounce — buzz only after this many consecutive "outside" readings
#define BUZZ_DEBOUNCE 3
int outsideCount = 0;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW — receives d1 from Anchor1 (id=1), d3 from Anchor2 (id=3)
typedef struct struct_message { int id; float distance; } struct_message;
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
    struct_message data;
    memcpy(&data, incomingData, sizeof(data));
    if (data.id == 1 && data.distance > 0) { d1 = data.distance; lastD1 = millis(); }
    if (data.id == 3 && data.distance > 0) { d3 = data.distance; lastD3 = millis(); }
}

// UWB Config
static dwt_config_t config = {5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1, DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD, (129+8-8), DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0};
static uint8_t tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W','A','V','2', 0xE0, 0, 0};
static uint8_t rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V','E','W','2', 0xE1, 0,0,0,0,0,0,0,0,0,0};
static uint8_t rx_buffer[20];
static uint8_t frame_seq_nb = 0;

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_register_recv_cb(OnDataRecv);

    // Register broadcast peer for SYNC
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    _fastSPI = SPISettings(16000000L, MSBFIRST, SPI_MODE0);
    spiBegin(PIN_IRQ, PIN_RST);
    spiSelect(PIN_SS);
    delay(2);

    while (!dwt_checkidlerc()) {
        Serial.println("IDLE FAILED, retrying...");
        delay(10);
    }
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
        Serial.println("INIT FAILED"); while(1);
    }
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
    if (dwt_configure(&config)) {
        Serial.println("CONFIG FAILED"); while(1);
    }
    dwt_configuretxrf(&txconfig_options);
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);
    dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
    dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
    dwt_setlnapamode(DWT_LNA_ENABLE);

    // Load Room Profile
    preferences.begin("uwb", false);
    currentRoomIndex = preferences.getInt("room", 0);
    if (currentRoomIndex >= NUM_ROOMS || currentRoomIndex < 0) {
        currentRoomIndex = 0;
    }
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    Serial.println("HUB — 3D Tracking Ready");
    Serial.print("Loaded Room Profile: ");
    Serial.println(rooms[currentRoomIndex].name);

    // Give time for Serial to flush, then print config for Python server
    delay(500);
    printRoomConfig();

    // Buzzer setup — active-HIGH, start silent
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
}

void loop() {
    // =============================================================
    // TIME SLOT 3 of 3  (600 ms shared cycle)
    // Hub sends SYNC at t=0, Anchor1 ranges at ~0ms, Anchor2 at 200ms,
    // Hub ranges at 400ms. Each slot is 200ms wide (~5ms used per ranging).
    // =============================================================
    uint32_t cycle_start = millis();

    // Check for BOOT button press
    if (digitalRead(BOOT_BUTTON_PIN) == LOW && (millis() - lastButtonPress > 500)) {
        currentRoomIndex = (currentRoomIndex + 1) % NUM_ROOMS;
        preferences.putInt("room", currentRoomIndex);
        lastButtonPress = millis();
        Serial.print("Switched to Room: ");
        Serial.println(rooms[currentRoomIndex].name);
        printRoomConfig();
    }

    // Broadcast SYNC to A1 and A2
    uint8_t sync_msg = 0xAA;
    esp_now_send(broadcastAddress, &sync_msg, 1);

    // Wait exactly 400ms for Hub's slot
    while (millis() - cycle_start < 400) { yield(); }

    // 1. Hub ranges to TAG (d2) via SS-TWR
    tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
    dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);
    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

    uint32_t status_reg;
    while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
             (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)));

    frame_seq_nb++;

    if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

        uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
        if (frame_len <= sizeof(rx_buffer)) {
            dwt_readrxdata(rx_buffer, frame_len, 0);
            rx_buffer[ALL_MSG_SN_IDX] = 0;

            if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0) {
                uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts_32, resp_tx_ts_32;
                int32_t rtd_init, rtd_resp;
                float clockOffsetRatio;

                poll_tx_ts = dwt_readtxtimestamplo32();
                resp_rx_ts = dwt_readrxtimestamplo32();
                clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);

                resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts_32);
                resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts_32);

                rtd_init = resp_rx_ts - poll_tx_ts;
                rtd_resp = resp_tx_ts_32 - poll_rx_ts_32;
                double tof = ((rtd_init - rtd_resp * (1.0 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
                d2 = tof * SPEED_OF_LIGHT;
                if (d2 < 0) d2 = 0;
                lastD2 = millis(); // mark d2 fresh
            }
        }
    } else {
        // UWB exchange failed — reset d2 so stale data doesn't linger
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        d2 = 0;
    }

    // 2. Print raw distances
    Serial.print("D_hub: "); Serial.print(d2, 2);
    Serial.print(" m | D_A1: "); Serial.print(d1, 2);
    Serial.print(" m | D_A2: "); Serial.print(d3, 2); Serial.print(" m");

    // 3. Trilateration — only when all three distances are fresh
    unsigned long now = millis();
    if (d1 > 0 && d2 > 0 && d3 > 0 &&
        (now - lastD1) < 6000 && (now - lastD2) < 6000 && (now - lastD3) < 6000) {

        RoomConfig rc = rooms[currentRoomIndex];

        double d1sq = d1 * d1; // A1
        double d2sq = d2 * d2; // Hub
        double d3sq = d3 * d3; // A2

        // GENERIC 2x2 TRILATERATION SOLVER
        // Eq1 (A1): (x - a1_x)^2 + (y - a1_y)^2 = d1^2
        // Eq2 (Hub): (x - hub_x)^2 + (y - hub_y)^2 = d2^2
        // Eq3 (A2): (x - a2_x)^2 + (y - a2_y)^2 = d3^2
        
        double A = 2 * (rc.a1_x - rc.hub_x);
        double B = 2 * (rc.a1_y - rc.hub_y);
        double C = d2sq - d1sq - (rc.hub_x * rc.hub_x) - (rc.hub_y * rc.hub_y) + (rc.a1_x * rc.a1_x) + (rc.a1_y * rc.a1_y);

        double D = 2 * (rc.a1_x - rc.a2_x);
        double E = 2 * (rc.a1_y - rc.a2_y);
        double F = d3sq - d1sq - (rc.a2_x * rc.a2_x) - (rc.a2_y * rc.a2_y) + (rc.a1_x * rc.a1_x) + (rc.a1_y * rc.a1_y);

        double det = A * E - B * D;
        double X = 0, Y = 0, Z = 0;

        if (abs(det) > 0.0001) {
            X = (C * E - B * F) / det;
            Y = (A * F - C * D) / det;
            
            double zSq = d1sq - (X - rc.a1_x)*(X - rc.a1_x) - (Y - rc.a1_y)*(Y - rc.a1_y);
            Z = rc.a1_z - sqrt(max(0.0, zSq));
        }

        // BOUNDARY CHECK
        // Using a simplified rectangular bounding check to support dynamic rooms safely
        bool outsideRoom = (X < 0.0 || Y < 0.0 || X > rc.max_x || Y > rc.max_y);
        if (outsideRoom) outsideCount++; else outsideCount = 0;

        if (outsideCount >= BUZZ_DEBOUNCE) {
            digitalWrite(BUZZER_PIN, HIGH);
            Serial.print("  [ALERT] Tag OUTSIDE room! [");
            Serial.print(outsideCount); Serial.print(" reads] ");
        } else {
            digitalWrite(BUZZER_PIN, LOW);
            if (outsideRoom) Serial.print("  [warn] outside (debouncing...) ");
        }

        // Clamp for display bounding box
        X = max(0.0, min((double)rc.max_x, X));
        Y = max(0.0, min((double)rc.max_y, Y));
        Z = max(0.0, min(rc.a1_z, Z));

        Serial.print("  ==> X: "); Serial.print(X, 2);
        Serial.print(" m  Y: ");   Serial.print(Y, 2);
        Serial.print(" m  Z: ");   Serial.print(Z, 2); Serial.println(" m");
    } else {
        // Not all fresh — keep buzzer silent
        digitalWrite(BUZZER_PIN, LOW);
        Serial.println("  ==> Waiting for all distances... [BUZZER SILENT]");
    }

    // Always broadcast room config periodically so late-starting Python servers can sync
    printRoomConfig();

    // Wait for end of 600ms cycle
    while (millis() - cycle_start < 600) { yield(); }
}
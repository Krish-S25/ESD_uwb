// -------------------------------------------------------
// ANCHOR2Final.ino
// 3D TRACKING — Room: 2.45 m (X)  x  3.04 m (Y)  x  1.42 m (Z)
// Anchor 2 position: (2.45, 3.04, 1.42) — mounted at wall, height 1.42 m  [id = 3]
// Hub      position: (1.45, 0, 1.42)
// Anchor 1 position: (0, 2.3, 1.42)                                         [id = 1]
// -------------------------------------------------------
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include "dw3000.h"

#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  5

#define MY_ID 3
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

uint8_t hubAddress[] = {0x94, 0x54, 0xC5, 0xAE, 0xEC, 0xC4};

typedef struct struct_message { int id; float distance; } struct_message;
struct_message myData;

static dwt_config_t config = {5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1, DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD, (129+8-8), DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0};
static uint8_t tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W','A','V', '3', 0xE0, 0, 0};
static uint8_t rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V','E','W', '3', 0xE1, 0,0,0,0,0,0,0,0,0,0};
static uint8_t rx_buffer[20];
static uint8_t frame_seq_nb = 0;

volatile uint32_t cycle_start = 0;
volatile bool sync_received = false;

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
    if (len == 1 && incomingData[0] == 0xAA) {
        cycle_start = millis();
        sync_received = true;
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, hubAddress, 6);
    esp_now_add_peer(&peerInfo);
    esp_now_register_recv_cb(OnDataRecv);

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

    Serial.println("Anchor 2 at (2.45, 3.04, 1.42) Ready");
}

void loop() {
    if (!sync_received) {
        yield();
        return;
    }

    // Wait exactly 200ms from SYNC for Slot 2
    while (millis() - cycle_start < 200) { yield(); }
    sync_received = false;

    // =============================================================
    // TIME SLOT 2 of 3  (600 ms shared cycle)
    // Anchor 2 ranges exactly 200ms after SYNC.
    // =============================================================
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
                float dist = tof * SPEED_OF_LIGHT; // meters
                if (dist < 0) dist = 0;

                myData.id = MY_ID;
                myData.distance = dist;
                esp_now_send(hubAddress, (uint8_t *) &myData, sizeof(myData));
                Serial.print("Sent D_A2: "); Serial.print(dist, 2); Serial.println(" m");
            }
        }
    } else {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        Serial.println("Anchor 2: No response");
    }
}
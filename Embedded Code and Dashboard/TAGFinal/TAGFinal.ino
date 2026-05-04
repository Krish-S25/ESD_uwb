/*
 * UWB TAG — ESP32 Version
 * Role: Smart SS-TWR Responder (Echoes Anchor IDs)
 */

#include <SPI.h>
#include "dw3000.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  5

#define TX_ANT_DLY                  16385
#define RX_ANT_DLY                  16385
#define ALL_MSG_COMMON_LEN          10
#define ALL_MSG_SN_IDX              2
#define RESP_MSG_POLL_RX_TS_IDX     10
#define RESP_MSG_RESP_TX_TS_IDX     14
#define POLL_RX_TO_RESP_TX_DLY_UUS  900 // Tag responds 900µs after receiving poll.
                                          // Anchors start listening at 300µs and timeout
                                          // at 3300µs — plenty of margin.

static dwt_config_t config = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (129 + 8 - 8), DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

static uint8_t rx_buffer[20];
static uint8_t tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', '0', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static uint8_t frame_seq_nb = 0;
static uint64_t poll_rx_ts;
static uint64_t resp_tx_ts;

extern dwt_txconfig_t txconfig_options;

// get_rx_timestamp_u64() and resp_msg_set_ts() are provided by the DW3000 library
// (dw3000_shared_functions.cpp) — do NOT redefine them here.

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Crucial to prevent power reboots
    Serial.begin(115200);
    delay(1000);

    // Clean SPI Initialization
    spiBegin(PIN_IRQ, PIN_RST);
    spiSelect(PIN_SS);
    delay(2);

    while (!dwt_checkidlerc()) {
        Serial.println("IDLE FAILED, retrying...");
        delay(10);
    }
    
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
        Serial.println("INIT FAILED");
        while (1);
    }
    
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
    
    if (dwt_configure(&config)) {
        Serial.println("CONFIG FAILED");
        while (1);
    }
    
    dwt_configuretxrf(&txconfig_options);
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);
    dwt_setlnapamode(DWT_LNA_ENABLE);

    Serial.println("=====================================");
    Serial.println("  SMART TAG (ESP32) — READY");
    Serial.println("=====================================");

    // Enable RX once here; loop re-enables after each exchange.
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

void loop() {
    // Wait for any activity: good frame, timeout, or error
    uint32_t status_reg;
    while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
             (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)));

    if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

        uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
        if (frame_len <= sizeof(rx_buffer)) {
            dwt_readrxdata(rx_buffer, frame_len, 0);

            if (rx_buffer[9] == 0xE0) { 
                uint32_t resp_tx_time;

                poll_rx_ts = get_rx_timestamp_u64();
                
                resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
                dwt_setdelayedtrxtime(resp_tx_time);
                
                resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

                resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
                resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);

                // Echo back anchor ID byte ('1','2','3') and sequence number
                tx_resp_msg[8] = rx_buffer[8]; 
                tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;

                dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
                dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);

                if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
                    while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK));
                    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
                    frame_seq_nb++;
                    
                    Serial.print("[TAG] Responded to anchor: ");
                    Serial.println((char)tx_resp_msg[8]);
                } else {
                    // Delayed TX missed window (shouldn't happen with 900µs delay)
                    // — clear and fall through to re-enable RX
                    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
                    Serial.println("[TAG] Delayed TX missed — re-enabling RX");
                }
            }
        }
    } else {
        // RX error or timeout — clear flags
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    }

    // Always re-enable RX to listen for the next poll
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}
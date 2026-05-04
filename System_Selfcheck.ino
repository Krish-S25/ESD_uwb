/*
 * BU03/DW3000 Final Hardware Self-Check
 */
#include <SPI.h>
#include "dw3000.h"

// ===================== VERIFIED PIN CONFIG =====================
#define PIN_SCK  18   // ESP32 GPIO18 -> BU03 Pin 20 (SPICLK)   
#define PIN_MISO 19   // ESP32 GPIO19 -> BU03 Pin 19 (SPIMISO)
#define PIN_MOSI 23   // ESP32 GPIO23 -> BU03 Pin 18 (SPIMOSI)
#define PIN_SS   5    // ESP32 GPIO5  -> BU03 Pin 17 (SPICSN)
#define PIN_RST  27   // ESP32 GPIO27 -> BU03 Pin 3  (RSTN)
#define PIN_IRQ  34   // ESP32 GPIO34 -> BU03 Pin 22 (IRQ/GPIO8)
//Make Wakeup mapping to gpio and control it for power conservation.
// ===============================================================

static dwt_config_t config = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (129 + 8 - 8), DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

extern dwt_txconfig_t txconfig_options;
extern SPISettings _fastSPI;

static uint8_t tx_test_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'U', 'W', 'B', '-', 'T', 'A', 'G', 0, 0};
static uint8_t seq = 0;
static bool init_ok = false;

bool runHardwareDiagnostic() {
    _fastSPI = SPISettings(16000000L, MSBFIRST, SPI_MODE0);
    
    Serial.println("\n--- Step 1: SPI Bus Initialization ---");
    // Explicitly assigning pins to the VSPI hardware block
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS); 
    spiBegin(PIN_IRQ, PIN_RST);
    spiSelect(PIN_SS);
    delay(20); 
    
    Serial.printf("  Mapping Verified -> SCK:%d, MISO:%d, MOSI:%d, CS:%d\n", PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);

    Serial.println("--- Step 2: Communication Check ---");
    uint32_t dev_id = dwt_readdevid();
    if (dev_id == 0xDECA0302) {
        Serial.println("  [PASS] DW3000 Detected (ID: 0xDECA0302)");
    } else {
        Serial.printf("  [FAIL] Received ID: 0x%08X (Check Wiring!)\n", dev_id);
        return false;
    }

    Serial.println("--- Step 3: Driver & Radio Init ---");
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
        Serial.println("  [FAIL] Driver initialization failed");
        return false;
    }
    
    if (dwt_configure(&config)) {
        Serial.println("  [FAIL] Radio configuration failed");
        return false;
    }
    
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
    dwt_configuretxrf(&txconfig_options);
    Serial.println("  [PASS] Radio configured on Channel 5");

    return true;
}

void setup() {
    Serial.begin(115200);
    while(!Serial); 
    Serial.println("\n**************************************");
    Serial.println("*    BU03 VERIFIED PIN DIAGNOSTIC    *");
    Serial.println("**************************************");

    init_ok = runHardwareDiagnostic();

    if (init_ok) {
        Serial.println("\nSUCCESS: All hardware pins verified.");
    } else {
        Serial.println("\nERROR: Verification failed. Retrying in 5s...");
    }
}

void loop() {
    if (!init_ok) {
        delay(5000);
        init_ok = runHardwareDiagnostic();
        return;
    }

    // Simple TX Heartbeat to prove communication is active
    tx_test_msg[2] = seq++;
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx_test_msg), tx_test_msg, 0);
    dwt_writetxfctrl(sizeof(tx_test_msg), 0, 0);
    dwt_starttx(DWT_START_TX_IMMEDIATE);

    while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK));
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);

    Serial.printf("Heartbeat %d sent.\n", seq);
    delay(1000);
}

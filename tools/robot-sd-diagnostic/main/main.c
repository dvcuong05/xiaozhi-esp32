#include <stdio.h>
#include <string.h>
#include "config.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static spi_device_handle_t dev;

static uint8_t exchange(uint8_t value) {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 8,
        .tx_data = {value},
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(dev, &t));
    return t.rx_data[0];
}

static void release_card(void) {
    gpio_set_level(SD_CS_PIN, 1);
    exchange(0xff);
}

static void sample_idle(void) {
    gpio_set_level(SD_CS_PIN, 1);
    unsigned zeros = 0, ones = 0;
    uint8_t first[8];
    for (int i = 0; i < 64; i++) {
        uint8_t value = exchange(0xff);
        zeros += value == 0;
        ones += value == 0xff;
        if (i < 8)
            first[i] = value;
    }
    printf("SDDIAG: CS high idle: zero bytes=%u FF bytes=%u other=%u first=", zeros, ones,
           64 - zeros - ones);
    for (int i = 0; i < 8; i++)
        printf("%02x", first[i]);
    printf("\n");
}

static uint8_t command(int cmd, uint32_t arg) {
    release_card();
    gpio_set_level(SD_CS_PIN, 0);
    exchange(0xff);
    uint8_t packet[] = {0x40 | cmd, arg >> 24, arg >> 16,
                        arg >> 8,   arg,       cmd == 0 ? 0x95 : (cmd == 8 ? 0x87 : 0x01)};
    for (size_t i = 0; i < sizeof(packet); i++)
        exchange(packet[i]);
    uint8_t r = 0xff;
    for (int i = 0; i < 32; i++) {
        r = exchange(0xff);
        if (!(r & 0x80))
            break;
    }
    return r;
}

static bool read_data(uint8_t* buffer, size_t size) {
    int64_t until = esp_timer_get_time() + 1000000;
    uint8_t token;
    unsigned attempts = 0;
    do {
        token = exchange(0xff);
        if (token != 0xff)
            break;
        if (++attempts % 256 == 0)
            vTaskDelay(1);
    } while (esp_timer_get_time() < until);
    if (token != 0xfe) {
        printf("SDDIAG: data token=0x%02x (expected 0xfe)\n", token);
        return false;
    }
    for (size_t i = 0; i < size; i++)
        buffer[i] = exchange(0xff);
    uint8_t crc_hi = exchange(0xff);
    uint8_t crc_lo = exchange(0xff);
    uint16_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= (uint16_t)buffer[i] << 8;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    uint16_t wire_crc = ((uint16_t)crc_hi << 8) | crc_lo;
    printf("SDDIAG: read %u bytes CRC16=%04x received=%04x %s\n", (unsigned)size, crc, wire_crc,
           crc == wire_crc ? "PASS" : "FAIL");
    return crc == wire_crc;
}

static void set_frequency(int hz) {
    if (dev)
        ESP_ERROR_CHECK(spi_bus_remove_device(dev));
    spi_device_interface_config_t cfg = {
        .clock_speed_hz = hz,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &cfg, &dev));
    printf("SDDIAG: SPI frequency=%d Hz\n", hz);
}

void app_main(void) {
    printf("SDDIAG: RAM read-only test CS=%d CLK=%d MOSI=%d MISO=%d\n", SD_CS_PIN, SD_SCK_PIN,
           SD_MOSI_PIN, SD_MISO_PIN);
    gpio_config_t cs = {.pin_bit_mask = 1ULL << SD_CS_PIN, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&cs));
    ESP_ERROR_CHECK(gpio_set_level(SD_CS_PIN, 1));
    spi_bus_config_t bus = {.mosi_io_num = SD_MOSI_PIN,
                            .miso_io_num = SD_MISO_PIN,
                            .sclk_io_num = SD_SCK_PIN,
                            .quadwp_io_num = -1,
                            .quadhd_io_num = -1};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));
    set_frequency(100000);
    ESP_ERROR_CHECK(gpio_set_pull_mode(SD_MISO_PIN, GPIO_FLOATING));
    vTaskDelay(pdMS_TO_TICKS(200));
    uint8_t r = 0xff;
    for (int phase = 0; phase < 2; phase++) {
        if (phase)
            ESP_ERROR_CHECK(gpio_set_pull_mode(SD_MISO_PIN, GPIO_PULLUP_ONLY));
        printf("SDDIAG: MISO internal pull-up=%s\n", phase ? "ON" : "OFF");
        sample_idle();
        for (int i = 0; i < 20; i++) {
            r = command(0, 0);
            release_card();
            if (r == 1)
                break;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        printf("SDDIAG: CMD0 R1=%02x\n", r);
        if (r == 1)
            break;
    }
    if (r != 1)
        goto done;
    r = command(8, 0x1aa);
    uint8_t r7[4];
    for (int i = 0; i < 4; i++)
        r7[i] = exchange(0xff);
    release_card();
    printf("SDDIAG: CMD8 R1=%02x R7=%02x%02x%02x%02x\n", r, r7[0], r7[1], r7[2], r7[3]);
    bool v2 = r == 1 && r7[2] == 1 && r7[3] == 0xaa;
    if (!v2 || r7[0] != 0 || r7[1] != 0) {
        printf("SDDIAG: malformed CMD8 response; cannot validate card initialization\n");
        goto done;
    }
    for (int i = 0; i < 200; i++) {
        command(55, 0);
        release_card();
        r = command(41, v2 ? 0x40000000 : 0);
        release_card();
        if (r == 0)
            break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    printf("SDDIAG: ACMD41 R1=%02x\n", r);
    if (r != 0)
        goto done;
    r = command(58, 0);
    uint8_t ocr[4];
    for (int i = 0; i < 4; i++)
        ocr[i] = exchange(0xff);
    release_card();
    printf("SDDIAG: CMD58 R1=%02x OCR=%02x%02x%02x%02x\n", r, ocr[0], ocr[1], ocr[2], ocr[3]);
    if (!(ocr[0] & 0x40)) {
        r = command(16, 512);
        release_card();
        printf("SDDIAG: CMD16 R1=%02x\n", r);
    }
    uint8_t data[512];
    r = command(17, 0);
    printf("SDDIAG: CMD17 LBA0 at initialization clock R1=%02x\n", r);
    if (r == 0 && read_data(data, 512))
        printf("SDDIAG: sector0 signature=%02x%02x\n", data[510], data[511]);
    release_card();
    for (int cmd = 10; cmd >= 9; cmd--) {
        r = command(cmd, 0);
        printf("SDDIAG: CMD%d R1=%02x\n", cmd, r);
        if (r == 0 && read_data(data, 16)) {
            printf("SDDIAG: register=");
            for (int j = 0; j < 16; j++)
                printf("%02x", data[j]);
            printf("\n");
        }
        release_card();
    }
    int frequencies[] = {400000, 1000000, 10000000};
    for (size_t i = 0; i < 3; i++) {
        set_frequency(frequencies[i]);
        r = command(17, 0);
        printf("SDDIAG: CMD17 LBA0 R1=%02x\n", r);
        if (r == 0 && read_data(data, 512))
            printf("SDDIAG: sector0 signature=%02x%02x\n", data[510], data[511]);
        release_card();
    }
    set_frequency(400000);
    r = command(55, 0);
    release_card();
    printf("SDDIAG: CMD55 before ACMD13 R1=%02x\n", r);
    r = command(13, 0);
    uint8_t status = exchange(0xff);
    printf("SDDIAG: ACMD13 R1=%02x second status=%02x\n", r, status);
    if (r == 0)
        read_data(data, 64);
    release_card();
done:
    release_card();
    printf("SDDIAG: DONE. No writes or formatting. Reset to restore production firmware.\n");
    fflush(stdout);
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

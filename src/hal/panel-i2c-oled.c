/*
 * I2C OLED panels: SH1107 128x128 (oled128) and SSD1306 128x64 (oled64).
 *
 * From LeekWallet's oled.c (SSD1306) and the LeekWallet Zcash firmware's
 * SH1107 support: the same framing (0x00 command, 0x40 data), the address
 * probe at 0x3C then 0x3D, and the legacy I2C driver, because the camera's
 * SCCB uses it and IDF 5.5 cannot mix the legacy and new drivers.
 *
 * The framebuffer holds palette indices; this converts them to the panel's
 * page format (one byte per column per 8 rows, bit 0 on top) at flush.
 */
#if !defined(LBS_QEMU) && (defined(LBS_DISPLAY_OLED128) || defined(LBS_DISPLAY_OLED64))

#include "panel.h"

#include <string.h>

#include "board.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "gfx.h"

static const char *TAG = "panel";

#define I2C_PORT    I2C_NUM_0
#define I2C_FREQ_HZ 400000
#define PAGES       (GFX_H / 8)

static uint8_t addr = 0x3C;
static bool ready;
static uint8_t page_buf[GFX_W];

static esp_err_t send_cmd(uint8_t cmd)
{
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = { 0x00, cmd };
    return i2c_master_write_to_device(I2C_PORT, addr, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t send_data(const uint8_t *data, size_t len)
{
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[1 + GFX_W];
    buf[0] = 0x40;
    while (len > 0) {
        size_t chunk = len > GFX_W ? GFX_W : len;
        memcpy(&buf[1], data, chunk);
        esp_err_t err = i2c_master_write_to_device(I2C_PORT, addr, buf, chunk + 1,
                                                   pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            return err;
        }
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

static bool present(uint8_t a)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(a << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

/* Pick the address, or log everything on the bus so a bring-up shows what
 * is actually wired. */
static bool probe(void)
{
    static const uint8_t candidates[] = { 0x3C, 0x3D };
    for (size_t i = 0; i < sizeof candidates; i++) {
        if (present(candidates[i])) {
            addr = candidates[i];
            return true;
        }
    }
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (present(a)) {
            ESP_LOGW(TAG, "I2C scan: device at 0x%02X", a);
            found++;
        }
    }
    ESP_LOGE(TAG, "no panel at 0x3C/0x3D on SDA=%d SCL=%d (%d other devices)%s",
             PIN_I2C_SDA, PIN_I2C_SCL, found,
             found ? "" : ": check power, GND, and SDA/SCL not swapped");
    return false;
}

static void compose_page(int page)
{
    for (int x = 0; x < GFX_W; x++) {
        uint8_t b = 0;
        for (int bit = 0; bit < 8; bit++) {
            if (gfx_role_lit(gfx_get(x, page * 8 + bit))) {
                b |= (uint8_t)(1u << bit);
            }
        }
        page_buf[x] = b;
    }
}

#if defined(LBS_DISPLAY_OLED128)
/* SH1107: init values from Adafruit's SH110X driver for 128x128, as in the
 * Zcash firmware. Page addressing: 0xB0+page, column 0, 128 bytes. */
static const uint8_t init_cmds[] = {
    0xAE,                                   /* display off */
    0xD5, 0x51,                             /* clock divide / oscillator */
    0x20,                                   /* page addressing */
    0x81, 0x4F,                             /* contrast */
    0xAD, 0x8A,                             /* DC-DC on */
    BOARD_SH1107_FLIP ? 0xA1 : 0xA0,        /* segment remap */
    BOARD_SH1107_FLIP ? 0xC8 : 0xC0,        /* COM scan direction */
    0xDC, 0x00,                             /* display start line */
    0xD3, 0x00,                             /* display offset */
    0xD9, 0x22,                             /* pre-charge */
    0xDB, 0x35,                             /* VCOMH */
    0xA8, 0x7F,                             /* multiplex: 128 rows */
    0xA4,                                   /* follow RAM */
    0xA6,                                   /* not inverted */
};
#define PANEL_NAME "SH1107"
#else
static const uint8_t init_cmds[] = {
    0xAE,       /* display off */
    0xD5, 0x80, /* clock */
    0xA8, 0x3F, /* multiplex 64 */
    0xD3, 0x00, /* offset */
    0x40,       /* start line */
    0x8D, 0x14, /* charge pump */
    0x20, 0x02, /* page addressing, as for the SH1107 */
    0xA1,       /* segment remap */
    0xC8,       /* COM scan remapped */
    0xDA, 0x12, /* COM pins */
    0x81, 0xCF, /* contrast */
    0xD9, 0xF1, /* pre-charge */
    0xDB, 0x40, /* VCOMH */
    0xA4,       /* follow RAM */
    0xA6,       /* not inverted */
};
#define PANEL_NAME "SSD1306"
#endif

esp_err_t panel_flush(void)
{
    for (int page = 0; page < PAGES; page++) {
        compose_page(page);
        send_cmd((uint8_t)(0xB0 + page));
        send_cmd(0x10);     /* column high nibble */
        send_cmd(0x00);     /* column low nibble */
        esp_err_t err = send_data(page_buf, GFX_W);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t panel_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_PORT, &conf);
    if (err == ESP_OK) {
        err = i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C setup failed: %s", esp_err_to_name(err));
        return err;
    }
    if (!probe()) {
        return ESP_ERR_NOT_FOUND;
    }
    ready = true;
    for (size_t i = 0; i < sizeof init_cmds; i++) {
        if (send_cmd(init_cmds[i]) != ESP_OK) {
            ESP_LOGE(TAG, PANEL_NAME " init cmd %02X failed", init_cmds[i]);
            ready = false;
            return ESP_FAIL;
        }
    }
    /* The panel powers up with noise in its RAM: clear before lighting. */
    gfx_clear(C_BG);
    panel_flush();
    send_cmd(0xAF);
    ESP_LOGI(TAG, PANEL_NAME " %dx%d at 0x%02X", GFX_W, GFX_H, addr);
    return ESP_OK;
}

bool panel_present(void) { return ready; }

void panel_set_brightness(int level, int max)
{
    /* Contrast from dim to the init value's full range. A camera reads an
       OLED best a step or two down from full: the lit pixels bloom less. */
    if (!ready || max <= 0) {
        return;
    }
    const int contrast = 0x10 + (0xFF - 0x10) * level / max;
    send_cmd(0x81);
    send_cmd((uint8_t)contrast);
}

#endif

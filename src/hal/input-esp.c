/*
 * Buttons and encoder on the board: one task polling every 1 ms.
 *
 * Polling, not interrupts, as LeekWallet and its Zcash firmware do: the
 * encoder decoder needs every state of a quadrature cycle, and 1 ms catches
 * them at any speed a hand turns an EC11. The button state machine and the
 * decoder are pure (src/core) and tested on the host; this file only reads
 * pins and supplies the time.
 */
#if !defined(LBS_QEMU)

#include "input-queue.h"

#include "board.h"
#include "driver/gpio.h"
#include "encoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "input";

static const int button_pins[4] = { PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_BACK, PIN_BTN_OK };
static const input_ev_t button_map[4] = { IN_UP, IN_DOWN, IN_BACK, IN_OK };

static input_buttons_t buttons;
static Encoder encoder;

static void sink(input_ev_t ev, void *ctx)
{
    (void)ctx;
    input_post(ev);
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t pressed = 0;
        for (int i = 0; i < 4; i++) {
            int level = gpio_get_level(button_pins[i]);
            if (level == (BOARD_BUTTONS_ACTIVE_HIGH ? 1 : 0)) {
                pressed |= 1u << i;
            }
        }
        input_buttons_poll(&buttons, esp_timer_get_time(), pressed, sink, NULL);

        int step = encoder_feed(&encoder, (uint8_t)gpio_get_level(PIN_ENC_CLK),
                                (uint8_t)gpio_get_level(PIN_ENC_DT));
        input_post(input_from_detent(step, BOARD_ENC_REVERSE));

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void input_hw_start(void)
{
    uint64_t mask = (1ULL << PIN_ENC_CLK) | (1ULL << PIN_ENC_DT);
    for (int i = 0; i < 4; i++) {
        mask |= 1ULL << button_pins[i];
    }
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = BOARD_BUTTONS_ACTIVE_HIGH ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    if (BOARD_BUTTONS_ACTIVE_HIGH) {
        /* The encoder contacts still pull to GND. */
        gpio_pullup_en(PIN_ENC_CLK);
        gpio_pullup_en(PIN_ENC_DT);
    }

    input_buttons_init(&buttons, 4, button_map);
    encoder_init(&encoder);
    /* 1 ms ticks need CONFIG_FREERTOS_HZ=1000 (sdkconfig.defaults). */
    xTaskCreate(poll_task, "input", 3072, NULL, 6, NULL);
    ESP_LOGI(TAG, "buttons UP=%d DOWN=%d BACK=%d OK=%d, encoder CLK=%d DT=%d%s",
             PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_BACK, PIN_BTN_OK,
             PIN_ENC_CLK, PIN_ENC_DT, BOARD_ENC_REVERSE ? " (reversed)" : "");
}

#endif

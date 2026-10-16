/*
 * Input queue. See input-queue.h.
 */
#include "input-queue.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "input";

/* Polling is 1 ms and a step fires at most once per detent, so 32 covers a
 * fast spin across a long blocking operation. */
#define QUEUE_LEN 32

static QueueHandle_t queue;
static uint32_t dropped;

void input_queue_init(void)
{
    if (queue == NULL) {
        queue = xQueueCreate(QUEUE_LEN, sizeof(input_ev_t));
    }
}

void input_post(input_ev_t ev)
{
    if (queue == NULL || ev == IN_NONE) {
        return;
    }
    if (xQueueSend(queue, &ev, 0) == pdTRUE) {
        return;
    }
    input_ev_t stale;
    if (xQueueReceive(queue, &stale, 0) == pdTRUE) {
        dropped++;
        ESP_LOGW(TAG, "queue full, dropped stale %s (total %lu)",
                 input_name(stale), (unsigned long)dropped);
    }
    xQueueSend(queue, &ev, 0);
}

bool input_wait(input_ev_t *ev, int timeout_ms)
{
    TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return queue && xQueueReceive(queue, ev, ticks) == pdTRUE;
}

void input_drain(void)
{
    if (queue) {
        xQueueReset(queue);
    }
}

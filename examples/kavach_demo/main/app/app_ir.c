/*
 * Universal IR remote: learn (receive) and send AC on/off via BSP IR RX/TX.
 * Saves codes to SPIFFS; sends on voice command or app_ir_send_ac_*().
 */
#include <stdio.h>
#include <string.h>
#include "sys/queue.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "bsp_board.h"
#include "ir_learn.h"
#include "ir_encoder.h"
#include "app_ir.h"
#include "gui/ui_kavach.h"  /* kavach_ui_set_status_async_ir, kavach_ui_set_light_async (from any task) */

static const char *TAG = "app_ir";

#define IR_RESOLUTION_HZ    1000000
#define IR_AC_ON_PATH      "/spiffs/ir_ac_on.cfg"
#define IR_AC_OFF_PATH     "/spiffs/ir_ac_off.cfg"

static ir_learn_handle_t s_ir_learn_handle = NULL;
static volatile bool s_ir_learn_active = false;
static app_ir_learn_done_cb_t s_learn_done_cb = NULL;
static void *s_learn_done_user = NULL;

static struct ir_learn_list_head s_learn_on_head;
static struct ir_learn_list_head s_learn_off_head;
static struct ir_learn_sub_list_head s_data_on;
static struct ir_learn_sub_list_head s_data_off;

static QueueHandle_t s_tx_queue = NULL;
static TaskHandle_t s_tx_task_handle = NULL;

static esp_err_t ir_save_cfg(const char *filepath, struct ir_learn_sub_list_head *cmd_list)
{
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed open file: %s", filepath);
        return ESP_FAIL;
    }
    uint8_t cmd_num = 0;
    ir_learn_sub_list_t *sub_it;
    SLIST_FOREACH(sub_it, cmd_list, next) {
        cmd_num++;
    }
    fwrite(&cmd_num, 1, sizeof(cmd_num), fp);
    cmd_num = 0;
    SLIST_FOREACH(sub_it, cmd_list, next) {
        uint32_t timediff = sub_it->timediff;
        fwrite(&timediff, 1, sizeof(uint32_t), fp);
        size_t symbol_num = sub_it->symbols.num_symbols;
        fwrite(&symbol_num, 1, sizeof(symbol_num), fp);
        fwrite(sub_it->symbols.received_symbols, 1, symbol_num * sizeof(rmt_symbol_word_t), fp);
        cmd_num++;
    }
    fclose(fp);
    return ESP_OK;
}

static esp_err_t ir_read_cfg(const char *filepath, struct ir_learn_sub_list_head *cmd_list)
{
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        return ESP_FAIL;
    }
    uint8_t total_cmd_num = 0;
    if (fread(&total_cmd_num, 1, sizeof(total_cmd_num), fp) != sizeof(total_cmd_num)) {
        fclose(fp);
        return ESP_FAIL;
    }
    for (int i = 0; i < total_cmd_num; i++) {
        uint32_t timediff;
        if (fread(&timediff, 1, sizeof(timediff), fp) != sizeof(timediff)) {
            break;
        }
        size_t num_symbols;
        if (fread(&num_symbols, 1, sizeof(num_symbols), fp) != sizeof(num_symbols)) {
            break;
        }
        rmt_symbol_word_t *symbols = heap_caps_malloc(num_symbols * sizeof(rmt_symbol_word_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!symbols) {
            break;
        }
        if (fread(symbols, 1, num_symbols * sizeof(rmt_symbol_word_t), fp) != num_symbols * sizeof(rmt_symbol_word_t)) {
            free(symbols);
            break;
        }
        rmt_rx_done_event_data_t evt = { .num_symbols = num_symbols, .received_symbols = symbols };
        ir_learn_add_sub_list_node(cmd_list, timediff, &evt);
        free(symbols);
    }
    fclose(fp);
    return ESP_OK;
}

static void ir_tx_raw(struct ir_learn_sub_list_head *list)
{
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 128,
        .trans_queue_depth = 4,
        .gpio_num = BSP_IR_TX_GPIO,
    };
    rmt_channel_handle_t tx_channel = NULL;
    if (rmt_new_tx_channel(&tx_cfg, &tx_channel) != ESP_OK) {
        return;
    }
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33f,
        .frequency_hz = 38000,
    };
    rmt_apply_carrier(tx_channel, &carrier_cfg);

    rmt_transmit_config_t transmit_config = { .loop_count = 0 };
    ir_encoder_config_t nec_cfg = { .resolution = IR_RESOLUTION_HZ };
    rmt_encoder_handle_t nec_encoder = NULL;
    if (ir_encoder_new(&nec_cfg, &nec_encoder) != ESP_OK) {
        rmt_del_channel(tx_channel);
        return;
    }
    rmt_enable(tx_channel);

    ir_learn_sub_list_t *sub_it;
    SLIST_FOREACH(sub_it, list, next) {
        vTaskDelay(pdMS_TO_TICKS(sub_it->timediff / 1000));
        rmt_transmit(tx_channel, nec_encoder, sub_it->symbols.received_symbols, sub_it->symbols.num_symbols, &transmit_config);
        rmt_tx_wait_all_done(tx_channel, -1);
    }

    rmt_disable(tx_channel);
    rmt_del_channel(tx_channel);
    nec_encoder->del(nec_encoder);
}

static void ir_tx_task(void *arg)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BSP_IR_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(BSP_IR_CTRL_GPIO, 0);

    struct ir_learn_sub_list_head *list = NULL;
    while (xQueueReceive(s_tx_queue, &list, portMAX_DELAY) == pdTRUE && list != NULL) {
        ir_tx_raw(list);
        ir_learn_clean_sub_data(list);
        free(list);
    }
    s_tx_task_handle = NULL;
    vTaskDelete(NULL);
}

static void send_ir_from_file(const char *path)
{
    struct ir_learn_sub_list_head *list = heap_caps_calloc(1, sizeof(struct ir_learn_sub_list_head), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!list) {
        return;
    }
    SLIST_INIT(list);
    if (ir_read_cfg(path, list) != ESP_OK) {
        free(list);
        return;
    }
    if (s_tx_queue) {
        xQueueSend(s_tx_queue, &list, 0);
    } else {
        ir_learn_clean_sub_data(list);
        free(list);
    }
}

static void save_result(struct ir_learn_sub_list_head *data_save, struct ir_learn_sub_list_head *data_src)
{
    ir_learn_sub_list_t *last = SLIST_FIRST(data_src);
    ir_learn_sub_list_t *it;
    while ((it = SLIST_NEXT(last, next)) != NULL) {
        last = it;
    }
    ir_learn_add_sub_list_node(data_save, last->timediff, &last->symbols);
}

static void ir_learn_cb(ir_learn_state_t state, uint8_t sub_step, struct ir_learn_sub_list_head *data)
{
    switch (state) {
    case IR_LEARN_STATE_READY:
        break;
    case IR_LEARN_STATE_END:
    case IR_LEARN_STATE_FAIL:
        {
            bool on_ok = (ir_learn_check_valid(&s_learn_on_head, &s_data_on) == ESP_OK);
            bool off_ok = (ir_learn_check_valid(&s_learn_off_head, &s_data_off) == ESP_OK);
            /* Duration/symbol errors from ir_learn are common for some remotes; we can still save and try. */
            bool have_on = (SLIST_FIRST(&s_data_on) != NULL);
            bool have_off = (SLIST_FIRST(&s_data_off) != NULL);
            if ((on_ok && off_ok) || (have_on && have_off)) {
                if (!on_ok || !off_ok) {
                    ESP_LOGW(TAG, "IR learn: validation had warnings, saving anyway (try voice AC on/off)");
                }
                ir_save_cfg(IR_AC_ON_PATH, &s_data_on);
                ir_save_cfg(IR_AC_OFF_PATH, &s_data_off);
                ESP_LOGI(TAG, "IR learn OK: AC on/off saved");
                if (s_learn_done_cb) {
                    s_learn_done_cb(true, s_learn_done_user);
                }
            } else {
                ESP_LOGW(TAG, "IR learn failed (on_ok=%d off_ok=%d, have_on=%d have_off=%d)", on_ok, off_ok, have_on, have_off);
                if (s_learn_done_cb) {
                    s_learn_done_cb(false, s_learn_done_user);
                }
            }
            ir_learn_clean_data(&s_learn_on_head);
            ir_learn_clean_data(&s_learn_off_head);
            ir_learn_clean_sub_data(&s_data_on);
            ir_learn_clean_sub_data(&s_data_off);
            ir_learn_stop(&s_ir_learn_handle);
            s_ir_learn_active = false;
            s_learn_done_cb = NULL;
            s_learn_done_user = NULL;
        }
        break;
    case IR_LEARN_STATE_EXIT:
        break;
    default: {
        /* ir_learn passes learned_count (1..4) for steps, not IR_LEARN_STATE_STEP */
        unsigned step = (unsigned)state;
        if (step >= 1 && step <= 4) {
            ir_learn_list_t *last;
            ir_learn_list_t *it;
            if (state % 2) {
                if (sub_step == 1) {
                    ir_learn_add_list_node(&s_learn_on_head);
                }
                last = SLIST_FIRST(&s_learn_on_head);
                while (last && (it = SLIST_NEXT(last, next)) != NULL) {
                    last = it;
                }
                if (last) {
                    save_result(&last->cmd_sub_node, data);
                    kavach_ui_set_status_async_ir("AC On received & saved. Now press AC Off.");
                    kavach_ui_set_light_async(KAVACH_LIGHT_COMMAND_OK);
                }
            } else {
                if (sub_step == 1) {
                    ir_learn_add_list_node(&s_learn_off_head);
                }
                last = SLIST_FIRST(&s_learn_off_head);
                while (last && (it = SLIST_NEXT(last, next)) != NULL) {
                    last = it;
                }
                if (last) {
                    save_result(&last->cmd_sub_node, data);
                    kavach_ui_set_status_async_ir("AC Off received & saved. Finishing...");
                    kavach_ui_set_light_async(KAVACH_LIGHT_COMMAND_OK);
                }
            }
        }
        break;
    }
    }
}

bool app_ir_learn_is_active(void)
{
    return s_ir_learn_active;
}

void app_ir_learn_start(app_ir_learn_done_cb_t cb, void *user_data)
{
    if (s_ir_learn_active) {
        return;
    }
    ir_learn_clean_data(&s_learn_on_head);
    ir_learn_clean_data(&s_learn_off_head);
    ir_learn_clean_sub_data(&s_data_on);
    ir_learn_clean_sub_data(&s_data_off);

    s_learn_done_cb = cb;
    s_learn_done_user = user_data;
    ir_learn_cfg_t cfg = {
        .learn_count = 4,
        .learn_gpio = BSP_IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution = IR_RESOLUTION_HZ,
        .task_stack = 4096,
        .task_priority = 5,
        .task_affinity = 1,
        .callback = ir_learn_cb,
    };
    if (ir_learn_new(&cfg, &s_ir_learn_handle) == ESP_OK) {
        s_ir_learn_active = true;
        ESP_LOGI(TAG, "IR learn started: point remote, press AC On then AC Off");
    } else {
        if (cb) {
            cb(false, user_data);
        }
    }
}

void app_ir_learn_stop(void)
{
    if (s_ir_learn_active && s_ir_learn_handle) {
        ir_learn_stop(&s_ir_learn_handle);
        s_ir_learn_active = false;
        if (s_learn_done_cb) {
            s_learn_done_cb(false, s_learn_done_user);
            s_learn_done_cb = NULL;
            s_learn_done_user = NULL;
        }
    }
}

bool app_ir_has_ac_codes(void)
{
    FILE *f = fopen(IR_AC_ON_PATH, "rb");
    if (!f) {
        return false;
    }
    fclose(f);
    f = fopen(IR_AC_OFF_PATH, "rb");
    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

void app_ir_send_ac_on(void)
{
    send_ir_from_file(IR_AC_ON_PATH);
}

void app_ir_send_ac_off(void)
{
    send_ir_from_file(IR_AC_OFF_PATH);
}

void app_ir_init(void)
{
    if (s_tx_queue == NULL) {
        s_tx_queue = xQueueCreate(4, sizeof(struct ir_learn_sub_list_head *));
        if (s_tx_queue) {
            xTaskCreate(ir_tx_task, "ir_tx", 3072, NULL, 5, &s_tx_task_handle);
        }
    }
}

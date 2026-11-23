#include <inttypes.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pid.h"

#define LED_GPIO          8
#define LDR_GPIO          4
#define BOOT_GPIO         0
#define CONTROL_PERIOD_MS 20
#define ADC_SAMPLES       16
#define ADC_MAX           4095.0f

static const float SETPOINTS[] = {30.0f, 50.0f, 70.0f};
static adc_oneshot_unit_handle_t adc;
static adc_channel_t adc_channel;

static void hardware_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t led = {
        .gpio_num = LED_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&led));

    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));

    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(LDR_GPIO, &unit, &adc_channel));
    const adc_oneshot_unit_init_cfg_t adc_config = {.unit_id = unit};
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_config, &adc));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, adc_channel, &channel_config));
}

static void set_led(float percent)
{
    const uint32_t duty = (uint32_t)(percent * 4095.0f / 100.0f + 0.5f);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

static float read_sensor(void)
{
    int total = 0;
    for (int i = 0; i < ADC_SAMPLES; ++i) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc, adc_channel, &raw));
        total += raw;
    }
    return (float)total / ADC_SAMPLES;
}

void app_main(void)
{
    hardware_init();

    // Map the fixed ADC range, 0-4095, to 0-100%.
    float light = 100.0f * read_sensor() / ADC_MAX;
    pid_state_t pid = {.previous_measurement = light};
    size_t preset = 0;

    int last_button = gpio_get_level(BOOT_GPIO);
    int stable_button = last_button;
    int64_t button_changed_us = esp_timer_get_time();
    int64_t previous_us = esp_timer_get_time();
    int64_t last_log_us = 0;
    TickType_t last_wake = xTaskGetTickCount();

    printf("time_ms,setpoint_pct,light_pct,pwm_pct\n");
    while (1) {
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        const float measured = 100.0f * read_sensor() / ADC_MAX;
        const int64_t now_us = esp_timer_get_time();
        const float dt = (float)(now_us - previous_us) / 1000000.0f;
        previous_us = now_us;

        // Smooth ADC noise to reduce fluctuations in the derivative term.
        light += 0.25f * (measured - light);

        const int button = gpio_get_level(BOOT_GPIO);
        if (button != last_button) {
            last_button = button;
            button_changed_us = now_us;
        }
        if (button != stable_button && now_us - button_changed_us >= 40000) {
            stable_button = button;
            // Advance the setpoint once per debounced BOOT release.
            if (button == 1) {
                preset = (preset + 1) % (sizeof SETPOINTS / sizeof SETPOINTS[0]);
            }
        }

        const float setpoint = SETPOINTS[preset];
        const float pwm = pid_update(&pid, setpoint, light, dt);
        set_led(pwm);

        if (now_us - last_log_us >= 100000) {
            last_log_us = now_us;
            printf("%" PRId64 ",%.1f,%.1f,%.1f\n",
                   now_us / 1000, setpoint, light, pwm);
        }
    }
}

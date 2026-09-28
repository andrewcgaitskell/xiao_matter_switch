#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_matter.h>
#include <esp_openthread.h>
#include <esp_openthread_types.h>
#include <openthread/thread.h>
#include "OpenthreadLauncher.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Wrap underlying drivers natively for the C++ linker
extern "C" {
    #include <driver/gpio.h>
    #include <iot_button.h>
    #include <button_gpio.h>
}

static const char *TAG = "XIAO_SWITCH";

// Touch sensor callback
static void touch_sensor_callback(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "Touch Sensor Single-Click Detected on Pin D10 (GPIO18)!");
}

extern "C" void app_main(void)
{
    // ============================================================================
    // STEP 1: FACTORY RESET - Erase all commissioning fabric data
    // ============================================================================
    ESP_LOGI(TAG, "=== FACTORY RESET: Erasing commissioning data ===");
    nvs_flash_erase();
    ESP_LOGI(TAG, "NVS flash erased. Device will start as uncommissioned.");

    // ============================================================================
    // STEP 2: Initialize NVS for Thread & Matter storage
    // ============================================================================
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS initialized successfully.");

    // ============================================================================
    // STEP 3: Configure button for momentary switch input
    // ============================================================================
    button_config_t btn_cfg;
    memset(&btn_cfg, 0, sizeof(button_config_t));
    btn_cfg.long_press_time = 1000;
    btn_cfg.short_press_time = 50;

    button_gpio_config_t gpio_cfg;
    memset(&gpio_cfg, 0, sizeof(button_gpio_config_t));
    gpio_cfg.gpio_num = GPIO_NUM_18;  // XIAO C6 pin D10 (GPIO18)
    gpio_cfg.active_level = 1;        // High logic on capacitive touch trigger

    button_handle_t btn_handle = nullptr;

    err = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn_handle);
    if (err == ESP_OK && btn_handle) {
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, nullptr, touch_sensor_callback, nullptr);
        ESP_LOGI(TAG, "Touch sensor initialized on GPIO18.");
    } else {
        ESP_LOGE(TAG, "Failed to initialize touch sensor!");
    }

    // ============================================================================
    // STEP 4: Create Matter Node
    // ============================================================================
    esp_matter::node::config_t node_config;
    esp_matter::node_t *node = esp_matter::node::create(&node_config, NULL, NULL);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node!");
        return;
    }
    ESP_LOGI(TAG, "Matter node created successfully.");

    // ============================================================================
    // STEP 5: Create On/Off Light Endpoint (simpler than generic_switch)
    // ============================================================================
    esp_matter::endpoint::on_off_light::config_t light_config;

    esp_matter::endpoint_t *endpoint = esp_matter::endpoint::on_off_light::create(
        node, &light_config, esp_matter::ENDPOINT_FLAG_NONE, NULL
    );

    if (endpoint) {
        ESP_LOGI(TAG, "On/Off Light endpoint created successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to create On/Off Light endpoint!");
        return;
    }

    // ============================================================================
    // STEP 6: Configure OpenThread (Matter over Thread)
    // ============================================================================
    esp_openthread_platform_config_t ot_config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_NONE,
        },
        .port_config = {
            .storage_partition_name = "nvs",
            .netif_queue_size = 10,
            .task_queue_size = 10,
        },
    };
    ESP_ERROR_CHECK(set_openthread_platform_config(&ot_config));
    ESP_LOGI(TAG, "OpenThread platform configured.");

    // ============================================================================
    // STEP 7: Start Matter stack
    // ============================================================================
    esp_matter::start(NULL);
    ESP_LOGI(TAG, "Matter stack started.");

    // Wait for the device to attach to the Thread network before advertising as ready.
    // This is important when the OTBR and Home Assistant are already up and running.
    ESP_LOGI(TAG, "Waiting for Thread network attach...");
    otInstance *ot_instance = esp_openthread_get_instance();
    if (ot_instance != NULL) {
        const uint32_t timeout_ms = 60000;
        uint32_t elapsed_ms = 0;

        while (elapsed_ms < timeout_ms) {
            otDeviceRole role = otThreadGetDeviceRole(ot_instance);
            if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER) {
                ESP_LOGI(TAG, "Thread network attached. Device role=%d", role);
                break;
            }

            ESP_LOGI(TAG, "Thread not attached yet; waiting... role=%d", role);
            vTaskDelay(pdMS_TO_TICKS(1000));
            elapsed_ms += 1000;
        }

        if (elapsed_ms >= timeout_ms) {
            ESP_LOGW(TAG, "Thread network not attached within timeout. Home Assistant may still be able to commission if the OTBR is reachable, but the device is not joined.");
        }
    } else {
        ESP_LOGW(TAG, "OpenThread instance unavailable; continuing without waiting.");
    }

    // ============================================================================
    // STEP 8: Ready for commissioning
    // ============================================================================
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Device READY for Home Assistant pairing!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Matter Setup Code (default): 20202021");
    ESP_LOGI(TAG, "Discriminator (default): 3840");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "In Home Assistant:");
    ESP_LOGI(TAG, "  Settings > Devices & Services > Matter");
    ESP_LOGI(TAG, "  Add Device > Setup Code: 20202021");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");

    ESP_LOGI(TAG, "Firmware boot complete. Waiting for pairing...");
}

#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_matter.h>
#include <esp_openthread_types.h>
#include "OpenthreadLauncher.h"
#include <string.h>

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
    // This ensures the device starts fresh and will advertise for pairing
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
    ESP_LOGI(TAG, "Matter node created.");

    // ============================================================================
    // STEP 5: Create Momentary Switch Endpoint
    // ============================================================================
    esp_matter::endpoint::generic_switch::config_t switch_config;
    switch_config.switch_cluster.feature_flags = esp_matter::cluster::switch_cluster::feature::momentary_switch::get_id();

    esp_matter::endpoint_t *endpoint = esp_matter::endpoint::generic_switch::create(
        node, &switch_config, esp_matter::ENDPOINT_FLAG_NONE, btn_handle
    );

    if (endpoint) {
        ESP_LOGI(TAG, "Momentary Generic Switch endpoint created.");
    } else {
        ESP_LOGE(TAG, "Failed to create Generic Switch endpoint!");
    }

    // ============================================================================
    // STEP 6: Configure OpenThread (Matter over Thread)
    // ============================================================================
    esp_openthread_platform_config_t ot_config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,  // Use ESP32-C6's native 802.15.4 radio
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_NONE,  // No CLI needed
        },
        .port_config = {
            .storage_partition_name = "nvs",  // Use NVS for Thread credentials
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

    // ============================================================================
    // STEP 8: Ready for commissioning
    // ============================================================================
    // Because NVS was erased in STEP 1, the device has no saved fabric.
    // The Matter stack will automatically advertise for commissioning.
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Device is READY for Home Assistant pairing!");
    ESP_LOGI(TAG, "Look in Home Assistant:");
    ESP_LOGI(TAG, "Settings > Devices & Services > Matter");
    ESP_LOGI(TAG, "========================================");

    ESP_LOGI(TAG, "Firmware boot complete. Waiting for pairing...");
}

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
    #include <button_gpio.h> // Required for modern GPIO device instantiation
}

static const char *TAG = "XIAO_SWITCH";

// Callback structure matching the modern signature context requirements
static void touch_sensor_callback(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "Touch Sensor Single-Click Detected on Pin D10 (GPIO18)!");
}

extern "C" void app_main(void)
{
    // 1. FACTORY RESET: Erase all commissioned fabric data from NVS
    ESP_LOGI(TAG, "Performing factory reset - erasing commissioning data...");
    nvs_flash_erase();
    
    // 2. Initialize safe local flash storage for Thread credentials
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // 3. Configure button timing profiles
    button_config_t btn_cfg;
    memset(&btn_cfg, 0, sizeof(button_config_t));
    btn_cfg.long_press_time = 1000;
    btn_cfg.short_press_time = 50;

    // 4. Configure physical GPIO hardware parameters separately
    button_gpio_config_t gpio_cfg;
    memset(&gpio_cfg, 0, sizeof(button_gpio_config_t));
    gpio_cfg.gpio_num = GPIO_NUM_18; // Maps to your physical D10 line on the XIAO C6
    gpio_cfg.active_level = 1;       // High output logic on capacitive touch trigger

    button_handle_t btn_handle = nullptr;

    // Instantiate the device using the modern factory driver interface API
    err = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn_handle);
    if (err == ESP_OK && btn_handle) {
        // Register single-click events passing the required nullptr event argument parameters
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, nullptr, touch_sensor_callback, nullptr);
        ESP_LOGI(TAG, "Hardware touch sensor successfully mapped to GPIO18 via Modern API.");
    } else {
        ESP_LOGE(TAG, "Failed to instantiate the hardware button module!");
    }

    // 5. Initialize your clean Matter Node profile
    esp_matter::node::config_t node_config;
    esp_matter::node_t *node = esp_matter::node::create(&node_config, NULL, NULL);

    // 6. Create an On/Off Light Switch endpoint configuration
    esp_matter::endpoint::generic_switch::config_t switch_config;

    // Explicitly seed the Momentary Switch feature flag inside the configuration struct
    // NOTE: verify this against your esp_matter version's switch_cluster::feature API —
    // the original pasted source had this line truncated.
    switch_config.switch_cluster.feature_flags = esp_matter::cluster::switch_cluster::feature::momentary_switch::get_id();

    // Instantiate the verified endpoint passing the configuration parameters
    esp_matter::endpoint_t *endpoint = esp_matter::endpoint::generic_switch::create(
        node, &switch_config, esp_matter::ENDPOINT_FLAG_NONE, btn_handle
    );

    if (endpoint) {
        ESP_LOGI(TAG, "Successfully instantiated Momentary Generic Switch Endpoint.");
    } else {
        ESP_LOGE(TAG, "Failed to instantiate Generic Switch Endpoint!");
    }

    // 7. Configure and hand off the OpenThread radio/host settings before starting the stack.
    //    This MUST run before esp_matter::start(), which spins up the FreeRTOS task that
    //    reads this config when it calls openthread_init_stack().
    esp_openthread_platform_config_t ot_config = {
        .radio_config = {
            .radio_mode = RADIO_MODE_NATIVE,      // Use the C6's built-in 802.15.4 radio
        },
        .host_config = {
            .host_connection_mode = HOST_CONNECTION_MODE_NONE, // No OT CLI console needed
        },
        .port_config = {
            .storage_partition_name = "nvs",      // Reuse the existing nvs partition
            .netif_queue_size = 10,
            .task_queue_size = 10,
        },
    };
    ESP_ERROR_CHECK(set_openthread_platform_config(&ot_config));

    // 8. Fire up the Thread mesh radio stack and Matter layers
    esp_matter::start(NULL);
    
    // 9. Open commissioning window so Home Assistant can discover and pair the device
    ESP_LOGI(TAG, "Opening commissioning window (15 minutes)...");
    esp_matter::commissioning_window_open(NULL);
    
    ESP_LOGI(TAG, "Matter-over-Thread firmware is live! Device is in commissioning mode - ready for Home Assistant pairing");
}


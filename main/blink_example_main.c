#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include "mdns.h" // Include the mDNS library
#include <stdlib.h> // For rand()

#define EXAMPLE_ESP_WIFI_SSID "Disco_Shirt"
#define EXAMPLE_MAX_STA_CONN 4

static const char *TAG = "example";
#define BLINK_GPIO 20

static led_strip_handle_t led_strip;
static bool is_flashing = true; // To control flashing state
static uint8_t current_red = 0, current_green = 0, current_blue = 0; // Store current color

// Define the rainbow colors (RGB)
static const uint8_t rainbow_colors[][3] = {
    {255, 0, 0},   // Red
    {255, 127, 0}, // Orange
    {255, 255, 0}, // Yellow
    {0, 255, 0},   // Green
    {0, 0, 255},   // Blue
    {75, 0, 130},  // Indigo
    {148, 0, 211}  // Violet
};

#define RAINBOW_COLOR_COUNT (sizeof(rainbow_colors) / sizeof(rainbow_colors[0]))
#define LED_COUNT 20 // Total number of LEDs
#define FLASH_INTERVAL pdMS_TO_TICKS(250) // Flash interval

static void configure_led(void) {
    ESP_LOGI(TAG, "Configured to control 20 addressable LEDs!");
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = LED_COUNT,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

void set_led_color(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel(led_strip, i, red, green, blue);
    }
    led_strip_refresh(led_strip);
}

void rainbow_flash_task(void *pvParameter) {
    while (is_flashing) {
        // Randomly determine how many LEDs to turn on (between 1 and LED_COUNT)
        int num_leds_on = rand() % LED_COUNT + 1; // At least one LED on
        int chosen_indices[LED_COUNT] = {0}; // To keep track of which LEDs are on

        // Randomly choose LEDs to light up
        for (int i = 0; i < num_leds_on; i++) {
            int led_index;
            do {
                led_index = rand() % LED_COUNT;
            } while (chosen_indices[led_index] == 1); // Avoid duplicates
            chosen_indices[led_index] = 1; // Mark this LED as chosen

            // Choose a random rainbow color
            int color_index = rand() % RAINBOW_COLOR_COUNT;
            uint8_t red = rainbow_colors[color_index][0];
            uint8_t green = rainbow_colors[color_index][1];
            uint8_t blue = rainbow_colors[color_index][2];

            // Set the chosen LED to the selected rainbow color
            led_strip_set_pixel(led_strip, led_index, red %40, green %40, blue %40);
        }

        // Refresh the LED strip to show changes
        led_strip_refresh(led_strip);

        // Turn off any LEDs that weren't chosen
        for (int i = 0; i < LED_COUNT; i++) {
            if (chosen_indices[i] == 0) {
                led_strip_set_pixel(led_strip, i, 0, 0, 0); // Turn off the LED
            }
        }

        vTaskDelay(FLASH_INTERVAL); // Flash every 500 ms
    }
    // Once finished, set to the last known color
    set_led_color(current_red, current_green, current_blue);
    vTaskDelete(NULL); // Delete the task when done
}

esp_err_t get_handler(httpd_req_t *req) {
    const char *response =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<style>"
        "body { font-family: Arial, sans-serif; font-size: 1.8em; }"
        "h2 { font-size: 3em; }"
        "label { display: block; margin-top: 20px; font-size: 2.2em; }"
        "input[type='number'] { width: 150px; padding: 15px; font-size: 1.8em; }"
        "input[type='submit'] { padding: 15px 30px; font-size: 2em; margin-top: 30px; }"
        "#status { margin-top: 30px; font-size: 1.8em; color: green; }"
        ".footer { margin-top: 50px; font-size: 1.6em; color: gray; text-align: center; }"
        "</style>"
        "</head>"
        "<body>"
        "<h2>Disco Shirt</h2>"
        "<form id=\"colorForm\">"
        "  <label for=\"red\">Red (0-60):</label>"
        "  <input type=\"number\" id=\"red\" name=\"red\" min=\"0\" max=\"60\"><br>"
        "  <label for=\"green\">Green (0-60):</label>"
        "  <input type=\"number\" id=\"green\" name=\"green\" min=\"0\" max=\"60\"><br>"
        "  <label for=\"blue\">Blue (0-60):</label>"
        "  <input type=\"number\" id=\"blue\" name=\"blue\" min=\"0\" max=\"60\"><br><br>"
        "  <input type=\"submit\" value=\"Set Color\">"
        "</form>"
        "<p id=\"status\"></p>"
        "<script>"
        "document.getElementById('colorForm').onsubmit = function(event) {"
        "  event.preventDefault();"
        "  const red = Math.min(60, parseInt(document.getElementById('red').value) || 0);"
        "  const green = Math.min(60, parseInt(document.getElementById('green').value) || 0);"
        "  const blue = Math.min(60, parseInt(document.getElementById('blue').value) || 0);"
        "  document.getElementById('red').value = red;"
        "  document.getElementById('green').value = green;"
        "  document.getElementById('blue').value = blue;"
        "  const params = `red=${red}&green=${green}&blue=${blue}`;"
        "  fetch('/set_color', {"
        "    method: 'POST',"
        "    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
        "    body: params"
        "  })"
        "  .then(response => response.text())"
        "  .then(text => {"
        "    document.getElementById('status').innerText = text;"
        "  })"
        "  .catch(error => {"
        "    document.getElementById('status').innerText = 'Error: ' + error;"
        "  });"
        "};"
        "</script>"
        "<div class=\"footer\">insta: @borisnotes</div>"
        "</body>"
        "</html>";
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

esp_err_t post_handler(httpd_req_t *req) {
    char buf[100];
    int red = 0, green = 0, blue = 0;

    int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf)));
    if (ret > 0) {
        buf[ret] = '\0';
        sscanf(buf, "red=%d&green=%d&blue=%d", &red, &green, &blue);
        
        // Cap the values to 60
        current_red = (uint8_t)(red > 60 ? 60 : red);
        current_green = (uint8_t)(green > 60 ? 60 : green);
        current_blue = (uint8_t)(blue > 60 ? 60 : blue);
        
        // Stop flashing and set the new color
        is_flashing = false;
        set_led_color(current_red, current_green, current_blue);
        ESP_LOGI(TAG, "Set color to R: %d, G: %d, B: %d", current_red, current_green, current_blue);
        httpd_resp_send(req, "Color updated", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_500(req);
    }
    return ESP_OK;
}

void start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_start(&server, &config);

    httpd_uri_t get_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_handler,
    };
    httpd_register_uri_handler(server, &get_uri);

    httpd_uri_t set_color_uri = {
        .uri = "/set_color",
        .method = HTTP_POST,
        .handler = post_handler,
    };
    httpd_register_uri_handler(server, &set_color_uri);
}

void wifi_init_softap(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap(); // Create the default AP netif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = "",
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Set hostname
    ESP_ERROR_CHECK(esp_netif_set_hostname(ap_netif, "party")); // Set hostname directly

    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("party"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 LED Control"));

    ESP_LOGI(TAG, "WiFi Access Point initialized. SSID:%s", EXAMPLE_ESP_WIFI_SSID);
}

void app_main(void) {
    configure_led();
    wifi_init_softap();
    start_webserver();

    // Start the rainbow flash task
    xTaskCreate(rainbow_flash_task, "rainbow_flash_task", 2048, NULL, 5, NULL);
}

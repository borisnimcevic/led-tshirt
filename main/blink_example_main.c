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

#define EXAMPLE_ESP_WIFI_SSID "ESP32_LED_Control"
#define EXAMPLE_ESP_WIFI_PASS "12345678"
#define EXAMPLE_MAX_STA_CONN 4

static const char *TAG = "example";

#define BLINK_GPIO 20

static led_strip_handle_t led_strip;

static void configure_led(void) {
  ESP_LOGI(TAG, "Configured to control 20 addressable LEDs!");
  /* LED strip initialization with the GPIO and pixels number*/
  led_strip_config_t strip_config = {
      .strip_gpio_num = BLINK_GPIO,
      .max_leds = 20, // Set the number of LEDs to 20
  };
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000, // 10MHz
      .flags.with_dma = false,
  };
  ESP_ERROR_CHECK(
      led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_spi_config_t spi_config = {
      .spi_bus = SPI2_HOST,
      .flags.with_dma = true,
  };
  ESP_ERROR_CHECK(
      led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
  /* Set all LEDs off to clear all pixels */
  led_strip_clear(led_strip);
}

// HTTP GET handler for serving the HTML form
esp_err_t get_handler(httpd_req_t *req) {
  const char *response = "<!DOCTYPE html>"
                         "<html>"
                         "<body>"
                         "<h2>RGB LED Control</h2>"
                         "<form action=\"/set_color\" method=\"POST\">"
                         "  <label for=\"red\">Red (0-255):</label><br>"
                         "  <input type=\"number\" id=\"red\" name=\"red\" "
                         "min=\"0\" max=\"255\"><br>"
                         "  <label for=\"green\">Green (0-255):</label><br>"
                         "  <input type=\"number\" id=\"green\" name=\"green\" "
                         "min=\"0\" max=\"255\"><br>"
                         "  <label for=\"blue\">Blue (0-255):</label><br>"
                         "  <input type=\"number\" id=\"blue\" name=\"blue\" "
                         "min=\"0\" max=\"255\"><br><br>"
                         "  <input type=\"submit\" value=\"Set Color\">"
                         "</form>"
                         "</body>"
                         "</html>";
  httpd_resp_send(req, response, strlen(response));
  return ESP_OK;
}

// Function to set RGB color on all LEDs
void set_led_color(uint8_t red, uint8_t green, uint8_t blue) {
  for (int i = 0; i < 20; i++) { // Loop through all 20 LEDs
    led_strip_set_pixel(led_strip, i, red, green, blue);
  }
  led_strip_refresh(led_strip);
}

esp_err_t post_handler(httpd_req_t *req) {
  char buf[100];
  int red = 0, green = 0, blue = 0;

  // Parse RGB values from form
  int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf)));
  if (ret > 0) {
    buf[ret] = '\0';
    sscanf(buf, "red=%d&green=%d&blue=%d", &red, &green, &blue);
    set_led_color((uint8_t)red, (uint8_t)green, (uint8_t)blue);
    ESP_LOGI(TAG, "Set color to R: %d, G: %d, B: %d", red, green, blue);
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

  // URI handler for GET requests
  httpd_uri_t get_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = get_handler,
  };
  httpd_register_uri_handler(server, &get_uri);

  // URI handler for POST requests to set color
  httpd_uri_t set_color_uri = {
      .uri = "/set_color",
      .method = HTTP_POST,
      .handler = post_handler,
  };
  httpd_register_uri_handler(server, &set_color_uri);
}

void wifi_init_softap(void) {
  // Initialize NVS
  ESP_ERROR_CHECK(nvs_flash_init());

  // Initialize the underlying TCP/IP stack
  ESP_ERROR_CHECK(esp_netif_init());

  // Create the default event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create default Wi-Fi AP
  esp_netif_create_default_wifi_ap();

  // Wi-Fi initialization and configuration
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {
      .ap =
          {
              .ssid = EXAMPLE_ESP_WIFI_SSID,
              .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
              .password = EXAMPLE_ESP_WIFI_PASS,
              .max_connection = EXAMPLE_MAX_STA_CONN,
              .authmode = WIFI_AUTH_WPA_WPA2_PSK,
          },
  };

  if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi Access Point initialized. SSID:%s Password:%s",
           EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void app_main(void) {

  /* Configure the peripheral according to the LED type */
  configure_led();
  wifi_init_softap();
  start_webserver();
}

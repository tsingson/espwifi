#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include "esp_netif.h"

// --- 配置区 (完全保持您原有定义) ---
#define PIN_D2 GPIO_NUM_2
#define ESP_WIFI_SSID "ESP32_wifi_web"
#define ESP_WIFI_PASS "12345678"
static const char *TAG = "ESP32_MAIN";

// --- 全局状态 (完全保持原样) ---
static volatile int d2_level = 0;
static portMUX_TYPE d2_spinlock = portMUX_INITIALIZER_UNLOCKED;

// --- 中断服务 (完全保持原样) ---
static void IRAM_ATTR gpio_isr_handler(void *arg) {
  int current_level = gpio_get_level(PIN_D2);
  portENTER_CRITICAL_ISR(&d2_spinlock);
  d2_level = current_level;
  portEXIT_CRITICAL_ISR(&d2_spinlock);
}

// --- HTTP Server (仅添加日志，逻辑完全不变) ---
static esp_err_t get_pin_handler(httpd_req_t *req) {
  int level;
  portENTER_CRITICAL(&d2_spinlock);
  level = d2_level;
  portEXIT_CRITICAL(&d2_spinlock);

  // 增加日志：记录每一次请求的情况，但不改变响应内容
  ESP_LOGI(TAG, "Request received. Current D2 level: %d", level);

  char resp[4];
  snprintf(resp, sizeof(resp), "%d", level);
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t uri = {
        .uri = "/get_pin", .method = HTTP_GET, .handler = get_pin_handler};
    httpd_register_uri_handler(server, &uri);
    ESP_LOGI(TAG, "HTTP Server 启动成功");
  }
  return server;
}

// --- WiFi 初始化 (保持您验证成功的 AP 逻辑) ---
void wifi_init_softap(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {
      .ap = {
          .ssid = ESP_WIFI_SSID,
          .ssid_len = strlen(ESP_WIFI_SSID),
          .channel = 1,
          .password = ESP_WIFI_PASS,
          .max_connection = 4,
          .authmode = WIFI_AUTH_WPA2_PSK,
      },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_softap finished.");

  // 启动 Web 服务
  start_webserver();
}

// --- 主程序 (完全复刻您调试通过的初始化顺序) ---
void app_main(void) {
  fflush(stdout);
  esp_log_level_set("*", ESP_LOG_INFO);

  ESP_LOGI(TAG, "系统启动...");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // GPIO 初始化 (严格保持原有结构)
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << PIN_D2),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .intr_type = GPIO_INTR_ANYEDGE};
  gpio_config(&io_conf);

  gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  gpio_isr_handler_add(PIN_D2, gpio_isr_handler, NULL);
  d2_level = gpio_get_level(PIN_D2);

  // 网络初始化
  wifi_init_softap();
}
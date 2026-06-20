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

// --- 配置区 ---
#define PIN_D2 GPIO_NUM_2
#define ESP_WIFI_SSID "ESP32_wifi_web"
#define ESP_WIFI_PASS "你的WiFi密码"
static const char *TAG = "ESP32_MAIN";

// --- 全局状态 ---
static volatile int d2_level = 0;
static portMUX_TYPE d2_spinlock = portMUX_INITIALIZER_UNLOCKED;

// --- 中断服务 (ISR) ---
static void IRAM_ATTR gpio_isr_handler(void *arg) {
  int current_level = gpio_get_level(PIN_D2);
  portENTER_CRITICAL_ISR(&d2_spinlock);
  d2_level = current_level;
  portEXIT_CRITICAL_ISR(&d2_spinlock);
}

// --- HTTP Server ---
static esp_err_t get_pin_handler(httpd_req_t *req) {
  int level;
  portENTER_CRITICAL(&d2_spinlock);
  level = d2_level;
  portEXIT_CRITICAL(&d2_spinlock);

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

// --- WiFi 事件与初始化 ---
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "WiFi 断开，尝试重连...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "WiFi 连接成功，IP 地址: " IPSTR, IP2STR(&event->ip_info.ip));
    start_webserver();
  }
}

void wifi_init_sta(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap(); // 创建默认的 WiFi AP 网络接口
  // esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                      &event_handler, NULL, NULL);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &event_handler, NULL, NULL);

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = ESP_WIFI_SSID,
              .password = ESP_WIFI_PASS,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM)); // 开启节能
  ESP_ERROR_CHECK(esp_wifi_start());
}

// --- 主程序 ---
void app_main(void) {

  // 设置终端输出波特率
  fflush(stdout);
  esp_log_level_set("*", ESP_LOG_INFO);

  ESP_LOGI(TAG, "系统启动...");

  // NVS 初始化
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // GPIO 初始化
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << PIN_D2),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .intr_type = GPIO_INTR_ANYEDGE};
  gpio_config(&io_conf);

  gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  gpio_isr_handler_add(PIN_D2, gpio_isr_handler, NULL);
  d2_level = gpio_get_level(PIN_D2);

  // 网络初始化
  wifi_init_sta();
}
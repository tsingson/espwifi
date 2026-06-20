#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>

#define PIN_D2 GPIO_NUM_2
static const char *TAG = "ESP32_STABLE";

// 使用静态变量存储状态，并定义一个临界区锁
static volatile int d2_level = 0;
static portMUX_TYPE d2_spinlock = portMUX_INITIALIZER_UNLOCKED;

// 中断服务函数：极简，仅负责更新状态
static void IRAM_ATTR gpio_isr_handler(void *arg) {
  int current_level = gpio_get_level(PIN_D2);
  // 使用临界区，防止中断与主循环冲突
  portENTER_CRITICAL_ISR(&d2_spinlock);
  d2_level = current_level;
  portEXIT_CRITICAL_ISR(&d2_spinlock);
}

// HTTP 处理函数：读取安全值
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

// 初始化 WiFi 与网络
void wifi_init_sta(void) {
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // 开启 WiFi 节能模式，降低功耗
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

  // ... 此处添加你的 WiFi 连接逻辑 ...
  ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void) {
  // 增加这一行，使用 TAG 输出日志
  ESP_LOGI(TAG, "Starting application...");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // GPIO 配置
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << PIN_D2),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .intr_type = GPIO_INTR_ANYEDGE};
  gpio_config(&io_conf);

  // 安装 ISR
  gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  gpio_isr_handler_add(PIN_D2, gpio_isr_handler, NULL);
  d2_level = gpio_get_level(PIN_D2);

  // 系统基础服务
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_init_sta();

  // 服务器配置
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t uri = {
        .uri = "/get_pin", .method = HTTP_GET, .handler = get_pin_handler};
    httpd_register_uri_handler(server, &uri);
  }
}
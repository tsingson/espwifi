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
#define ESP_WIFI_PASS "12345678"
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
/**
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

*/

void wifi_init_sta(void)
{
    EventGroupHandle_t s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());//tcpip协议初始化

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();//创建sta对象

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();//初始化结构体
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));//通过上面结构体的配置去初始化wifi

    esp_event_handler_instance_t instance_any_id;//用于取消相应的事件
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,//表示捕获所有wifi事件
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,//获取到ip事件
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {//填充结构体成员
        .sta = {
            .ssid = ESP_WIFI_SSID, //你的wifi名称
            .password = ESP_WIFI_PASS ,//你的wifi密码
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */

            // .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,//加密等级
            // .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            // .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP) );//设置sta模式
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );//将结构体配置设置进去
    ESP_ERROR_CHECK(esp_wifi_start() );//启动wifi

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    // EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
    //         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
    //         pdFALSE,
    //         pdFALSE,
    //         portMAX_DELAY);
    //
    // /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
    //  * happened. */
    // if (bits & WIFI_CONNECTED_BIT) {
    //     ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
    //              EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    // } else if (bits & WIFI_FAIL_BIT) {
    //     ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
    //              EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    // } else {
    //     ESP_LOGE(TAG, "UNEXPECTED EVENT");
    // }
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
/**
 *
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "math.h"
#include <esp_http_server.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>

// Pins and defs
#define STATUS_PIN 26
#define ALARM_INDICATOR_PIN 25
#define EMERGENCY_BUTTON_PIN 4
#define HEART_RATE_TIMESTAMP_PIN 33
#define HEART_RATE_PIN 32
#define HEART_RATE_ADC_CHANNEL ADC1_CHANNEL_4
#define TACHYCARDIA_THRESHOLD 3000

/**
 *     Bonus, uncomment to starve the system with a short period and too many sensor readings
 */
//#define bonus 1

#define MAX_COUNT_SEM 10

// Handles for semaphores and mutex
SemaphoreHandle_t sem_emergency_call;
SemaphoreHandle_t sem_vitals_alert;
SemaphoreHandle_t print_mutex;
QueueSetHandle_t event_queue_set;

// Global variables
static const char *TAG = "RTS_MED_DEMO";
volatile int led_state = 0;
volatile int current_bpm = 0;
volatile bool sensor_alert = false; // Tracks the heart rate monitor
volatile bool manual_alert = false; // Tracks the physical & web buttons

#define EXAMPLE_ESP_WIFI_SSID "SpectrumSetup-3F"
#define EXAMPLE_ESP_WIFI_PASS "funnydeer853"
#define EXAMPLE_ESP_MAXIMUM_RETRY 5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

/* Google Gemini was used to edit the html, as an embedded programmer it's not my responsibility to make the web interface or know how to,
 * so this simply serves as a proof of concept for the company should they decide to move forward with the overall product since that's
 * what's actually important for the functionality of the system.
 */
const char index_html[] = "<!DOCTYPE html><html><head><style type=\"text/css\">html {  font-family: Arial;  display: inline-block;  margin: 0px auto;  text-align: center;}h1{  color: #070812;  padding: 2vh;}.button {  display: inline-block;  background-color: #b30000;  border: none;  border-radius: 4px;  color: white;  padding: 16px 40px;  text-decoration: none;  font-size: 30px;  margin: 2px;  cursor: pointer;}.button2 {  background-color: #364cf4;}.content {   padding: 50px;}.card-grid {  max-width: 800px;  margin: 0 auto;  display: grid;  grid-gap: 2rem;  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));}.card {  background-color: white;  box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);}.card-title {  font-size: 1.2rem;  font-weight: bold;  color: #034078}</style>"
"<script>"
"function fetchStatus() {"
"  fetch('/status').then(response => response.json()).then(data => {"
"    let statusEl = document.getElementById('intervention-status');"
"    let bpmEl = document.getElementById('bpm-display');" // Get the new BPM element
"    bpmEl.innerHTML = 'Heart Rate: <strong>' + data.bpm + ' BPM</strong>';" // Update the BPM text
"    if(data.state === 1) {"
"      statusEl.innerHTML = 'Intervention: <strong style=\"color:red;\">ACTIVE</strong>';"
"    } else {"
"      statusEl.innerHTML = 'Intervention: <strong style=\"color:green;\">NOMINAL</strong>';"
"    }"
"  });"
"}"
"setInterval(fetchStatus, 17);"
"function sendCmd(cmd) {"
"  fetch('/' + cmd).then(() => fetchStatus());"
"}"
"</script>"
"<title>ESP32 WEB SERVER</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><link rel=\"icon\" href=\"data:,\"><link rel=\"stylesheet\" href=\"https://use.fontawesome.com/releases/v5.7.2/css/all.css\" integrity=\"sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr\" crossorigin=\"anonymous\"></head><body>"
"<h2>Medical Intervention Override</h2><div class=\"content\"><div class=\"card-grid\"><div class=\"card\">"
"<p><i class=\"fas fa-heartbeat fa-2x\" style=\"color:#c81919;\"></i> <strong>Live Vitals</strong></p>"
"<p id=\"bpm-display\" style=\"font-size: 1.5em; margin: 10px 0;\">Heart Rate: <strong>-- BPM</strong></p>" // New HTML element
"<p id=\"intervention-status\">Intervention: <strong>...</strong></p>"
"<p><button class=\"button\" onclick=\"sendCmd('led2on')\">ALERT</button> <button class=\"button button2\" onclick=\"sendCmd('led2off')\">CLEAR</button></p>"
"</div></div></div></body></html>";

volatile int SEMCNT = 0;

// Function declarations
void heart_rate_monitor_task(void *pvParameters);
void patient_heartbeat_task(void *pvParameters);
void IRAM_ATTR emergency_button_isr(void *pvParameters);
void medical_event_handler_task(void *pvParameters);

void connect_wifi(void);
esp_err_t send_web_page(httpd_req_t *req);
esp_err_t get_req_handler(httpd_req_t *req);
esp_err_t status_handler(httpd_req_t *req);
esp_err_t led_on_handler(httpd_req_t *req);
esp_err_t led_off_handler(httpd_req_t *req);
httpd_handle_t setup_server(void);

void app_main(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  
  // Initialize Wi-Fi and Web Server
  ESP_LOGI(TAG, "Starting Wi-Fi Connection...");
  connect_wifi();
  ESP_LOGI(TAG, "Starting Web Server...");
  setup_server();
  
  // Initialize pins
  gpio_reset_pin(STATUS_PIN);
  gpio_set_direction(STATUS_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(STATUS_PIN, 1);
  
  gpio_reset_pin(ALARM_INDICATOR_PIN);
  gpio_set_direction(ALARM_INDICATOR_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(ALARM_INDICATOR_PIN, 1);
  
  gpio_reset_pin(HEART_RATE_TIMESTAMP_PIN);
  gpio_set_direction(HEART_RATE_TIMESTAMP_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(HEART_RATE_TIMESTAMP_PIN, 0);
  
  gpio_reset_pin(HEART_RATE_PIN);
  gpio_set_direction(HEART_RATE_PIN, GPIO_MODE_INPUT);
  
  gpio_reset_pin(EMERGENCY_BUTTON_PIN);
  gpio_set_direction(EMERGENCY_BUTTON_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(EMERGENCY_BUTTON_PIN, GPIO_PULLUP_ONLY);
  gpio_set_intr_type(EMERGENCY_BUTTON_PIN, GPIO_INTR_NEGEDGE);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(EMERGENCY_BUTTON_PIN, emergency_button_isr, NULL);
  
  // TODO 0c: Attach the three SemaphoreHandle_t defined earlier
  // (sem_emergency_call, sem_vitals_alert, print_mutex) to appropriate Semaphores.
  // Setup semaphores and other task interfaces
  sem_emergency_call = xSemaphoreCreateBinary();
  sem_vitals_alert = xSemaphoreCreateCounting(MAX_COUNT_SEM, 0);
  print_mutex = xSemaphoreCreateMutex();
  event_queue_set = xQueueCreateSet(MAX_COUNT_SEM + 1);
  xQueueAddToSet(sem_vitals_alert, event_queue_set);
  xQueueAddToSet(sem_emergency_call, event_queue_set);
  
  // Create tasks
  xTaskCreate(patient_heartbeat_task, "heartbeat", 2048, NULL, 1, NULL);
  xTaskCreate(heart_rate_monitor_task, "vitals", 2048, NULL, 2, NULL);
  xTaskCreate(medical_event_handler_task, "med_handler", 2048, NULL, 2, NULL);
}

void patient_heartbeat_task(void *pvParameters) {
  bool heartbeat_status = false;
  while(1) {
    heartbeat_status = !heartbeat_status;
    gpio_set_level(STATUS_PIN, heartbeat_status);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void emergency_button_task(void *pvParameters) {
  int state_new;
  int state_old = gpio_get_level(EMERGENCY_BUTTON_PIN);
  TickType_t last_state_change = xTaskGetTickCount();
  TickType_t debounce_time = pdMS_TO_TICKS(50);
  
  while (1) {
    state_new = gpio_get_level(EMERGENCY_BUTTON_PIN);
    
    // TODO 4a: Add addtional logic to prevent bounce effect
    if(state_new != state_old) {
      if((xTaskGetTickCount() - last_state_change) > debounce_time) {
        if (state_new == 0 ){
          xSemaphoreGive(sem_emergency_call);
          manual_alert = true;
          //TODO 4b: Add a console print indicating button was pressed (mutex protected)
          xSemaphoreTake(print_mutex, portMAX_DELAY);
          printf("Nurse Station: Emergency call received from Room 1!\n");
          xSemaphoreGive(print_mutex);
        }
        state_old = state_new;
        last_state_change = xTaskGetTickCount();
      }
    } else {
      last_state_change = xTaskGetTickCount();
    }
    
    vTaskDelay(pdMS_TO_TICKS(10)); // Do Not Modify This Delay!
  }
}

void heart_rate_monitor_task(void *pvParameters) {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(HEART_RATE_ADC_CHANNEL, ADC_ATTEN_DB_11);
#define VAL_AVERAGES 2
  int val_new, val_old, val;
  int val_sum = 0;
  int val_avg[VAL_AVERAGES];
  int val_idx = 0;
  bool tachycardia_detected = false;
  bool timestamp_state = false;
  
  for(int i = 0; i < VAL_AVERAGES; i++) {
    val = adc1_get_raw(HEART_RATE_ADC_CHANNEL);
    val_avg[i] = val;
    val_sum += val;
  }
  val_new = val_sum / VAL_AVERAGES;
  val_old = val_new;
  
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(17); // Strict 17ms period
  
  while(1){
    gpio_set_level(HEART_RATE_TIMESTAMP_PIN, timestamp_state);
    timestamp_state = !timestamp_state;
    val = adc1_get_raw(HEART_RATE_ADC_CHANNEL);
    val_sum -= val_avg[val_idx];
    val_sum += val;
    val_avg[val_idx++] = val;
    val_new = val_sum / VAL_AVERAGES;
    current_bpm = 40 + ((val_new * 160) / 4095);
    
    // A bit of noise filtering
    if(abs(val_new - val_old) > 3) {
      xSemaphoreTake(print_mutex, portMAX_DELAY);
      printf("Vitals Monitor: Current BPM Metric: %d\n", val_new);
      xSemaphoreGive(print_mutex);
      
      if (val_new > TACHYCARDIA_THRESHOLD && tachycardia_detected == false) {
        tachycardia_detected = true;
        sensor_alert = true;
        if(SEMCNT < MAX_COUNT_SEM+1) SEMCNT++; // DO NOT REMOVE THIS LINE
        xSemaphoreGive(sem_vitals_alert);
      }
      else if (val_new <= TACHYCARDIA_THRESHOLD) {
        tachycardia_detected = false;
        sensor_alert = false;
      }
      val_old = val_new;
    }
//    vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    if(val_idx >= VAL_AVERAGES) val_idx = 0;
  }
}

void medical_event_handler_task(void *pvParameters) {
  QueueSetMemberHandle_t activated_member;
  while (1) {
    activated_member = xQueueSelectFromSet(event_queue_set, portMAX_DELAY);
    if (activated_member == sem_vitals_alert) {
      int alert_count = uxSemaphoreGetCount(sem_vitals_alert);
      
      // Handle Vitals Alert
      if(alert_count > 0) {
        if (xSemaphoreTake(sem_vitals_alert, 0)) {
          SEMCNT--;  // DO NOT MODIFY THIS LINE
          xSemaphoreTake(print_mutex, portMAX_DELAY);
          printf("CRITICAL: Tachycardia detected! Total alerts pending: %d\n", alert_count);
          xSemaphoreGive(print_mutex);
          
          gpio_set_level(ALARM_INDICATOR_PIN, 0);
          vTaskDelay(pdMS_TO_TICKS((esp_random() % 5000))); // Random response time to a sensor alert, upto 5 seconds for the nurse to respond
          gpio_set_level(ALARM_INDICATOR_PIN, 1);
          // Reset alert after
          sensor_alert = 0;
        }
      }
    } else if (activated_member == sem_emergency_call) {
      // Handle Emergency Button
      if (xSemaphoreTake(sem_emergency_call, 0)) {
        xSemaphoreTake(print_mutex, portMAX_DELAY);
        printf("EVENT: Manual Emergency Intervention Requested!\n");
        xSemaphoreGive(print_mutex);
        
        gpio_set_level(ALARM_INDICATOR_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS((esp_random() % 20000))); // Random response time to a manual alert, upto 20 seconds for the nurse to respond
        gpio_set_level(ALARM_INDICATOR_PIN, 1);
        // Reset alert after
        manual_alert = 0;
        gpio_intr_enable(EMERGENCY_BUTTON_PIN);
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void emergency_button_isr(void *pvParameters) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(sem_emergency_call, &xHigherPriorityTaskWoken);
  gpio_intr_disable(EMERGENCY_BUTTON_PIN);
  manual_alert = 1;
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// --- Wi-Fi Event Handler ---
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

// Wi-Fi Setup
void connect_wifi(void) {
  s_wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
  
  wifi_config_t wifi_config = {
    .sta = {
      .ssid = EXAMPLE_ESP_WIFI_SSID,
      .password = EXAMPLE_ESP_WIFI_PASS,
    },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  
  ESP_LOGI(TAG, "wifi_init_sta finished.");
  
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
  
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }
  vEventGroupDelete(s_wifi_event_group);
}

// HTTP Server Handlers
esp_err_t get_req_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN); // Gemini
}

esp_err_t status_handler(httpd_req_t *req) {
  led_state = (sensor_alert || manual_alert) ? 1 : 0;
  
  // Gemini told me how to set this up
  char json_resp[64];
  snprintf(json_resp, sizeof(json_resp), "{\"state\": %d, \"bpm\": %d}", led_state, current_bpm);
  
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json_resp, HTTPD_RESP_USE_STRLEN);
}

esp_err_t led_on_handler(httpd_req_t *req) {
  // Act as a Remote Emergency Button
  xSemaphoreGive(sem_emergency_call);
  manual_alert = true;
  return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

esp_err_t led_off_handler(httpd_req_t *req) {
  manual_alert = false;
  return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = get_req_handler, .user_ctx = NULL};
httpd_uri_t uri_on = { .uri = "/led2on", .method = HTTP_GET, .handler = led_on_handler, .user_ctx = NULL};
httpd_uri_t uri_off = { .uri = "/led2off", .method = HTTP_GET, .handler = led_off_handler, .user_ctx = NULL};
httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL}; // Gemini

httpd_handle_t setup_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 1024;
  config.max_resp_headers = 1024;
  
  httpd_handle_t server = NULL;
  
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &uri_get);
    httpd_register_uri_handler(server, &uri_on);
    httpd_register_uri_handler(server, &uri_off);
    httpd_register_uri_handler(server, &uri_status); // Gemini
  }
  return server;
}

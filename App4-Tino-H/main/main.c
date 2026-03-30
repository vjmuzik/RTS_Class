/**
 *
 *
 *
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "math.h"

#define STATUS_PIN 26
#define ALARM_INDICATOR_PIN 25
#define EMERGENCY_BUTTON_PIN 4
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

volatile int SEMCNT = 0;

void heart_rate_monitor_task(void *pvParameters);
void patient_heartbeat_task(void *pvParameters);
void emergency_button_task(void *pvParameters);
void medical_event_handler_task(void *pvParameters);

void app_main(void)
{
  gpio_reset_pin(STATUS_PIN);
  gpio_set_direction(STATUS_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(STATUS_PIN, 1);
  
  gpio_reset_pin(ALARM_INDICATOR_PIN);
  gpio_set_direction(ALARM_INDICATOR_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(ALARM_INDICATOR_PIN, 1);
  
  gpio_reset_pin(HEART_RATE_PIN);
  gpio_set_direction(HEART_RATE_PIN, GPIO_MODE_INPUT);
  
  gpio_reset_pin(EMERGENCY_BUTTON_PIN);
  gpio_set_direction(EMERGENCY_BUTTON_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(EMERGENCY_BUTTON_PIN, GPIO_PULLUP_ONLY);
  
  // TODO 0c: Attach the three SemaphoreHandle_t defined earlier
  // (sem_emergency_call, sem_vitals_alert, print_mutex) to appropriate Semaphores.
  sem_emergency_call = xSemaphoreCreateBinary();
  sem_vitals_alert = xSemaphoreCreateCounting(MAX_COUNT_SEM, 0);
  print_mutex = xSemaphoreCreateMutex();
  
#ifdef bonus
  xTaskCreatePinnedToCore(patient_heartbeat_task, "heartbeat", 2048, NULL, 1, NULL,0);
  xTaskCreatePinnedToCore(heart_rate_monitor_task, "vitals", 4096, NULL, 4, NULL,0);
  xTaskCreatePinnedToCore(emergency_button_task, "emergency_btn", 2048, NULL, 3, NULL,0);
  xTaskCreatePinnedToCore(medical_event_handler_task, "med_handler", 2048, NULL, 2, NULL,0);
#else
  xTaskCreate(patient_heartbeat_task, "heartbeat", 2048, NULL, 1, NULL);
  xTaskCreate(heart_rate_monitor_task, "vitals", 2048, NULL, 2, NULL);
  xTaskCreate(emergency_button_task, "emergency_btn", 2048, NULL, 3, NULL);
  xTaskCreate(medical_event_handler_task, "med_handler", 2048, NULL, 2, NULL);
#endif
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
#ifdef bonus
#define VAL_AVERAGES 100
#else
#define VAL_AVERAGES 4
#endif
  int val_new, val_old, val;
  int val_sum = 0;
  int val_avg[VAL_AVERAGES];
  int val_idx = 0;
  bool tachycardia_detected = false;
  
  for(int i = 0; i < VAL_AVERAGES; i++) {
    val = adc1_get_raw(HEART_RATE_ADC_CHANNEL);
    val_avg[i] = val;
    val_sum += val;
  }
  val_new = val_sum / VAL_AVERAGES;
  val_old = val_new;
  
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(100); // Strict 100ms period
  
  while(1){
    val = adc1_get_raw(HEART_RATE_ADC_CHANNEL);
    val_sum -= val_avg[val_idx];
    val_sum += val;
    val_avg[val_idx++] = val;
    val_new = val_sum / VAL_AVERAGES;
    
    // A bit of noise filtering
#ifdef bonus
    if(val_new != val_old) {
#else
    if(abs(val_new - val_old) > 3) {
#endif
      xSemaphoreTake(print_mutex, portMAX_DELAY);
      printf("Vitals Monitor: Current BPM Metric: %d\n", val_new);
      xSemaphoreGive(print_mutex);
      
      if (val_new > TACHYCARDIA_THRESHOLD && tachycardia_detected == false) {
        tachycardia_detected = true;
        if(SEMCNT < MAX_COUNT_SEM+1) SEMCNT++; // DO NOT REMOVE THIS LINE
        xSemaphoreGive(sem_vitals_alert);
      }
      else if (val_new <= TACHYCARDIA_THRESHOLD) {
        tachycardia_detected = false;
      }
      val_old = val_new;
    }
//    vTaskDelay(pdMS_TO_TICKS(100));
#ifdef bonus
    if(val_idx >= VAL_AVERAGES) {
      val_idx = 0;
      vTaskDelayUntil(&xLastWakeTime, 1);
      xSemaphoreTake(print_mutex, portMAX_DELAY);
      printf("Vitals Monitor: Current BPM Metric: %d\n", val_new);
      xSemaphoreGive(print_mutex);
    }
#else
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    if(val_idx >= VAL_AVERAGES) val_idx = 0;
#endif
  }
}

void medical_event_handler_task(void *pvParameters) {
  while (1) {
    int alert_count = uxSemaphoreGetCount(sem_vitals_alert);
    
    // Handle Vitals Alert
    if(alert_count > 0) {
      if (xSemaphoreTake(sem_vitals_alert, 0)) {
        SEMCNT--;  // DO NOT MODIFY THIS LINE
        xSemaphoreTake(print_mutex, portMAX_DELAY);
        printf("CRITICAL: Tachycardia detected! Total alerts pending: %d\n", alert_count);
        xSemaphoreGive(print_mutex);
        
        gpio_set_level(ALARM_INDICATOR_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(ALARM_INDICATOR_PIN, 1);
      }
    }
    
    // Handle Emergency Button
    if (xSemaphoreTake(sem_emergency_call, 0)) {
      xSemaphoreTake(print_mutex, portMAX_DELAY);
      printf("EVENT: Manual Emergency Intervention Requested!\n");
      xSemaphoreGive(print_mutex);
      
      gpio_set_level(ALARM_INDICATOR_PIN, 0);
      vTaskDelay(pdMS_TO_TICKS(300));
      gpio_set_level(ALARM_INDICATOR_PIN, 1);
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

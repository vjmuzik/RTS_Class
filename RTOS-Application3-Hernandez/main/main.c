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
#include <float.h>

#define LED_PIN 26
#define BUTTON_PIN 4
#define LDR_ADC_CHANNEL ADC1_CHANNEL_4

#define LOG_BUFFER_SIZE 50


/**
 *     Bonus:
 *     A minor delay in the logger task  and disabling the interrupt until the log has been made handles potential mechanical bouncing on the button to prevent immediate re-triggering of the semaphore
 *     Better to implemenet harware debugging with a low-pass filter and schmitt trigger for more robustness
 *     Wokwi may not be able to do this if they have have not implemented the ability to simulate capacitors and resistors interacting to form a low-pass filter
 *     Or if they don't have any schmitt trigger components, some microcontrollers have those built-in to their digital inputs that you can turn on and use
 *     I'm uncertain if the ESP32 itself has those specifically or not though.
 */

// Semaphores
SemaphoreHandle_t buttonSem;
SemaphoreHandle_t print_mutex;

// Shared variables
volatile float sensor_log[LOG_BUFFER_SIZE];
volatile int log_index = 0;
volatile int log_count = 0;

// Function declarations
void IRAM_ATTR button_isr_handler(void* arg);
void led_blink_task(void *pvParameters);
void console_print_task(void *pvParameters);
void sensor_sunlight_task(void *pvParameters);
void logger_task(void *pvParameters);

void app_main(void) {
  // Setup LED
  gpio_reset_pin(LED_PIN);
  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_PIN, 0);
  
  // Setup button
  gpio_reset_pin(BUTTON_PIN);
  gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
  gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE);
  
  // Install button ise
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
  
  // Setup semaphores
  buttonSem = xSemaphoreCreateBinary();
  print_mutex = xSemaphoreCreateMutex();
  
  // Create tasks
  xTaskCreatePinnedToCore(led_blink_task, "led_blink", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(console_print_task, "telemetry_print", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(sensor_sunlight_task, "solar_monitor", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(logger_task, "data_logger", 4096, NULL, 3, NULL, 1);
}

void button_isr_handler(void* arg) {
  // Notify semaphore
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(buttonSem, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  // Disable the button interrupt until the log is sent to prevent bouncing
  gpio_intr_disable(BUTTON_PIN);
}

void led_blink_task(void *pvParameters) {
  bool led_state = false;
  while(1) {
    led_state = !led_state;
    gpio_set_level(LED_PIN, led_state);
    vTaskDelay(pdMS_TO_TICKS(1400)); // Toggle every 1.4 seconds
  }
}

void console_print_task(void *pvParameters) {
  while(1) {
    xSemaphoreTake(print_mutex, portMAX_DELAY);
    printf("Telemetry: Satellite systems nominal. Solar arrays tracking.\n");
    xSemaphoreGive(print_mutex);
    
    vTaskDelay(pdMS_TO_TICKS(7000)); // Print every 7 seconds
  }
}

void sensor_sunlight_task(void *pvParameters) {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(LDR_ADC_CHANNEL, ADC_ATTEN_DB_11);
  
  // Variables to compute LUX
  int raw;
  const float Vsource = 3.3;
  float Vmeasure = 0.;
  float Vmeasured = 0.;
  float Rmeasure = 0.;
  const float Rfixed = 100000;
  float lux = 0.;
  
  const float gamma = 0.7;
  const int Rl = 50;
  
  // Pre-fill the readings array with an initial sample to avoid startup anomaly
  for(int i = 0; i < LOG_BUFFER_SIZE; ++i) {
    raw =  adc1_get_raw(LDR_ADC_CHANNEL);
    if (raw <= 0) raw = 1;
    Vmeasure = (raw/4096.0) * Vsource;
    Rmeasure = (2000.0 * Vmeasure)/(1-(Vmeasure/Vsource));
    Vmeasured = (Rmeasure/(Rfixed+Rmeasure)) * Vsource;
    lux = ((Rl * pow(10,3) * pow(10,gamma))/Rmeasure) * (1/gamma);
    sensor_log[i] = lux;
  }
  
  
  const TickType_t periodTicks = pdMS_TO_TICKS(100);
  TickType_t lastWakeTime = xTaskGetTickCount();
  
  while (1) {
    // Read current sensor value
    raw = adc1_get_raw(LDR_ADC_CHANNEL);
    if (raw <= 0) raw = 1;
    
    // Calculate LUX based on your provided formulas
    Vmeasure = (raw/4096.0) * Vsource;
    Rmeasure = (2000.0 * Vmeasure)/(1-(Vmeasure/Vsource));
    Vmeasured = (Rmeasure/(Rfixed+Rmeasure)) * Vsource;
    lux = ((Rl * pow(10,3) * pow(10,gamma))/Rmeasure) * (1/gamma);
    
    // Storing the raw 0-4095 value per the assignment instructions
    sensor_log[log_index] = lux;
    log_index = (log_index + 1) % LOG_BUFFER_SIZE;
    if (log_count < LOG_BUFFER_SIZE) {
      log_count++;
    }
    
    // Use vTaskDelayUntil for strict timing
    vTaskDelayUntil(&lastWakeTime, periodTicks);
  }
}

void logger_task(void *pvParameters) {
  while(1) {
    // Block while waiting for button
    xSemaphoreTake(buttonSem, portMAX_DELAY);
    
    xSemaphoreTake(print_mutex, portMAX_DELAY);
    printf("\n=== GROUND COMMAND: COMPRESSING & DOWNLOADING SOLAR LOG ===\n");
    
    if (log_count == 0) {
      printf("Error: Log buffer is empty!\n");
    } else {
      float min = FLT_MAX;
      float max = -FLT_MAX;
      float sum = 0;
      int exceedance_count = 0;
      int current_count = log_count;
      
      // "Compress" the buffer by calculating summary stats
      for (int i = 0; i < current_count; i++) {
        float val = sensor_log[i];
        if (val < min) min = val;
        if (val > max) max = val;
        if (val > 650) exceedance_count++; // Random threshold
        sum += val;
      }
      
      float avg = sum / current_count;
      
      printf("Total Readings Captured: %d\n", current_count);
      printf("Maximum Lux:  %f\n", max);
      printf("Minimum Lux:  %f\n", min);
      printf("Average Lux:  %f\n", avg);
      printf("High Intensity Spikes:   %d\n", exceedance_count);
      
      log_count = 0;
      log_index = 0;
    }
    printf("===========================================================\n\n");
    xSemaphoreGive(print_mutex);
    
    // A minor delay and reenabling the button interrupt
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_intr_enable(BUTTON_PIN);
  }
}


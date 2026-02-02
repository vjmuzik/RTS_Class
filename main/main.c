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

#define BEACON_PIN 2

void status_beacon_task(void *pvParameters);
void status_print_task(void *pvParameters);

void app_main(void)
{
  gpio_reset_pin(BEACON_PIN);
  gpio_set_direction(BEACON_PIN, GPIO_MODE_OUTPUT);
  
  xTaskCreate(status_beacon_task, "StatusBeaconTask", 2048, NULL, 1, NULL);
  xTaskCreate(status_print_task, "StatusPrintTask", 2048, NULL, 1, NULL);
}

void status_beacon_task(void *pvParameters) {
  bool beacon_status = false;
  while(1) {
    beacon_status = !beacon_status;
    gpio_set_level(BEACON_PIN, beacon_status);
    
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void status_print_task(void *pvParameters) {
  while(1) {
    printf("System status: alive    Uptime: %lu ms\n", (unsigned long)(xTaskGetTickCount()*portTICK_PERIOD_MS));
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
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

#define LED_PIN 2

void blink_task(void *pvParameters);
void print_task(void *pvParameters);

void app_main(void)
{
  printf("Hello world!\n");
  
  gpio_reset_pin(LED_PIN);
  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
  
  xTaskCreate(blink_task, "BlinkTask", 2048, NULL, 1, NULL);
  xTaskCreate(print_task, "PrintTask", 2048, NULL, 1, NULL);
}

void blink_task(void *pvParameters) {
  bool state = false;
  while(1) {
    state = !state;
    gpio_set_level(GPIO_NUM_2, state);
    
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void print_task(void *pvParameters) {
  while(1) {
    printf("System alive, time=%lu ms\n", (unsigned long)(xTaskGetTickCount()*portTICK_PERIOD_MS));
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

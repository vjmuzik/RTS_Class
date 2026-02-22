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

/**
 *     Bonus, uncomment to starve the system with a short period and too many sensor readings
 */
//#define bonus 1

#define BEACON_PIN 2
#define LDR_PIN 32
#define LDR_ADC_CHANNEL ADC1_CHANNEL_4

void sensor_sunlight_task(void *pvParameters);
void led_beacon_task(void *pvParameters);
void status_print_task(void *pvParameters);

void app_main(void)
{
  gpio_reset_pin(BEACON_PIN);
  gpio_set_direction(BEACON_PIN, GPIO_MODE_OUTPUT);
  
  gpio_reset_pin(LDR_PIN);
  gpio_set_direction(LDR_PIN, GPIO_MODE_INPUT);
  
  xTaskCreatePinnedToCore(sensor_sunlight_task, "StatusPrintTask", 4096, NULL, 3, NULL, 1);  // sensor task
  xTaskCreatePinnedToCore(led_beacon_task, "StatusBeaconTask", 2048, NULL, 2, NULL, 1); // led task
  xTaskCreatePinnedToCore(status_print_task, "StatusPrintTask", 2048, NULL, 0, NULL, 1); // print task
}

void led_beacon_task(void *pvParameters) {
  bool beacon_status = false;
  while(1) {
    beacon_status = !beacon_status;
    gpio_set_level(BEACON_PIN, beacon_status);
    
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void status_print_task(void *pvParameters) {
  while(1) {
    printf("System status: alive    Uptime: %lu ms\n", (unsigned long)(xTaskGetTickCount()*portTICK_PERIOD_MS));
    vTaskDelay(pdMS_TO_TICKS(1000));
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
  // Variables for moving average
  int idx = 0;
  float sum = 0;
  const float gamma = 0.7;
  const int Rl = 50;
  
  //TODO11a consider where AVG_WINDOW is defined, it could be here, or global value
  const int AVG_WINDOW = 10;
  const int SENSOR_THRESHOLD = 500;
  int luxreadings[AVG_WINDOW];
  
  //See TODO 99
  // Pre-fill the readings array with an initial sample to avoid startup anomaly
  for(int i = 0; i < AVG_WINDOW; ++i) {
    raw =  adc1_get_raw(LDR_ADC_CHANNEL);
    Vmeasure = (raw/4096.0) * Vsource; //TODO11b correct this with the equation seen earlier
    Rmeasure = (2000.0 * Vmeasure)/(1-(Vmeasure/Vsource)); //TODO11c correct this with the equation seen earlier
    Vmeasured = (Rmeasure/(Rfixed+Rmeasure)) * Vsource; //TODO11b correct this with the equation seen earlier
    lux = ((Rl * pow(10,3) * pow(10,gamma))/Rmeasure) * (1/gamma); //TODO11d correct this with the equation seen earlier
    luxreadings[i] = lux;
    sum += luxreadings[i];
  }
  

#ifdef bonus
  const TickType_t periodTicks = pdMS_TO_TICKS(10); // e.g. 10 ms period, starve the system by reading the sensor too quickly
#else
  const TickType_t periodTicks = pdMS_TO_TICKS(500); // e.g. 500 ms period
#endif
  
  TickType_t lastWakeTime = xTaskGetTickCount(); // initialize last wake time
  
  while (1) {
#ifdef bonus
    for(int32_t i = 0; i<10000; i++) {
#endif
      // Read current sensor value
      raw = adc1_get_raw(LDR_ADC_CHANNEL);
      //    printf("**raw**: Sensor %d\n", raw);
      
      // Compute LUX
      Vmeasure = (raw/4096.0) * Vsource; //TODO11e correct this with the equation seen earlier
      Rmeasure = (2000.0 * Vmeasure)/(1-(Vmeasure/Vsource)); //TODO11f correct this with the equation seen earlier
      Vmeasured = (Rmeasure/(Rfixed+Rmeasure)) * Vsource; //TODO11b correct this with the equation seen earlier
      lux = ((Rl * pow(10,3) * pow(10,gamma))/Rmeasure) * 1/gamma; //TODO11g correct this with the equation seen earlier
                                                                   //    printf("**vmeas**: Sensor %f\n", Vmeasure);
                                                                   //    printf("**rmes**: Sensor %f\n", Rmeasure);
                                                                   //    printf("**vmeasd**: Sensor %f\n", Vmeasured);
                                                                   //    printf("**lux**: Sensor %f\n", lux);
      
      // Update moving average buffer
      sum -= luxreadings[idx];       // remove oldest value from sum
      
      luxreadings[idx] = lux;        // place new reading
      sum += lux;                 // add new value to sum
      idx = (idx + 1) % AVG_WINDOW;
      int avg = sum / AVG_WINDOW; // compute average
      
      //TODO11h Check threshold and print alert if exceeded or below based on context
      if (avg <= SENSOR_THRESHOLD) {
        printf("**Alert**: Sensor average %d exceeds threshold %d, eclipse immanent!\n", avg, SENSOR_THRESHOLD);
      } else {
        //TODO11i
        // (you could print the avg value for debugging)
        printf("**avg**: Sensor %d\n", avg);
      }
      //TODO11j: Print out time period [to help with answering Eng/Analysis quetionst (hint check Application Solution #1 )
      //https://wokwi.com/projects/430683087703949313
      //TODO11k Replace vTaskDelay with vTaskDelayUntil with parameters &lastWakeTime and periodTicks
#ifdef bonus
    }
#endif
    vTaskDelayUntil(&lastWakeTime, periodTicks);
  }
}

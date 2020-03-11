#ifndef UNIT_TEST

#ifdef DEBUG_SERIAL
#define DEBUG_OUT Serial
#endif
#define PRINTSTREAM_FALLBACK
#include "Debug.hpp"

#include <Arduino.h>
#include <VescData.h>
#include <elapsedMillis.h>
#include <rom/rtc.h> // for reset reason
#include <Smoothed.h>

// used in TFT_eSPI library as alternate SPI port (HSPI?)
// #define SOFT_SPI_MOSI_PIN 19 // Blue
// #define SOFT_SPI_MISO_PIN 23 // Orange
// #define SOFT_SPI_SCK_PIN 18  // Yellow
#define SOFT_SPI_MOSI_PIN 13 // Blue
#define SOFT_SPI_MISO_PIN 12 // Orange
#define SOFT_SPI_SCK_PIN 15  // Yellow

#define NRF_CE 26
#define NRF_CS 33

#include <RF24Network.h>
#include <NRF24L01Lib.h>

#include <TFT_eSPI.h>
#include <Wire.h>
#include <Preferences.h>

//------------------------------------------------------------------

#define COMMS_BOARD 00
#define COMMS_CONTROLLER 01

VescData board_packet, old_board_packet;
ControllerData controller_packet;
ControllerConfig controller_config;
//------------------------------------------------------------------

NRF24L01Lib nrf24;

RF24 radio(NRF_CE, NRF_CS);
RF24Network network(radio);

//------------------------------------------------------------------

#define NUM_RETRIES 5
#ifndef SEND_TO_BOARD_INTERVAL
#define SEND_TO_BOARD_INTERVAL 200
#endif
//------------------------------------------------------------------

#define LCD_WIDTH 240
#define LCD_HEIGHT 135

TFT_eSPI tft = TFT_eSPI(LCD_HEIGHT, LCD_WIDTH); // Invoke custom library
//------------------------------------------------------------------

class Stats
{
public:
  unsigned long total_failed;
  unsigned long consecutive_resps;
  RESET_REASON reset_reason_core0;
  RESET_REASON reset_reason_core1;
  uint16_t soft_resets = 0;
} stats;

#define STORE_STATS "stats"
#define STORE_STATS_SOFT_RSTS "soft resets"
Preferences statsStore;

elapsedMillis
    since_sent_to_board,
    since_read_trigger;

uint16_t remote_battery_percent = 0;

#define SMOOTH_OVER_MILLIS 2000

//------------------------------------------------------------

xQueueHandle xDisplayChangeEventQueue;
xQueueHandle xCommsStateEventQueue;

//------------------------------------------------------------------

#include <Button2.h>

#define BUTTON_35 35
Button2 button35(BUTTON_35);

#include <utils.h>
#include <screens.h>
// #include <menu_system.h>
#include <comms_connected_state.h>

// #include <display_task_0.h>
#include <nrf_comms.h>

#include <features/battery_measure.h>
#include <core1.h>
#include <peripherals.h>
// #include <flashingNeopixel.h>

//---------------------------------------------------------------

#include <FSRThrottleLib.h>

FSRPin brake(/*pin*/ FSR_BRAKE_PIN, FSR_MIN_RAW, FSR_MAX_RAW, 0, 127);
FSRPin accel(/*pin*/ FSR_ACCEL_PIN, FSR_MIN_RAW, FSR_MAX_RAW, 255, 127);
FSRThrottleLib throttle(&accel, &brake);

#include <throttle.h>

//---------------------------------------------------------------

void setup()
{
  Serial.begin(115200);

  statsStore.begin(STORE_STATS, /*read-only*/ false);
  stats.soft_resets = statsStore.getUInt(STORE_STATS_SOFT_RSTS, 0);

  stats.reset_reason_core0 = rtc_get_reset_reason(0);
  stats.reset_reason_core1 = rtc_get_reset_reason(1);

  Serial.printf("CPU0 reset reason: %s\n", get_reset_reason_text(stats.reset_reason_core0));
  Serial.printf("CPU1 reset reason: %s\n", get_reset_reason_text(stats.reset_reason_core1));

  if (stats.reset_reason_core0 == RESET_REASON::SW_CPU_RESET)
  {
    stats.soft_resets++;
    statsStore.putUInt(STORE_STATS_SOFT_RSTS, stats.soft_resets);
    Serial.printf("RESET!!! =========> %d\n", stats.soft_resets);
  }
  else if (stats.reset_reason_core0 == RESET_REASON::POWERON_RESET)
  {
    stats.soft_resets = 0;
    statsStore.putUInt(STORE_STATS_SOFT_RSTS, stats.soft_resets);
    Serial.printf("Storage: cleared resets\n");
  }
  statsStore.end();

  nrf24.begin(&radio, &network, COMMS_CONTROLLER, packet_available_cb);

  Serial.printf("FSR_MIN_RAW %d, FSR_MAX_RAW %d \n", FSR_MIN_RAW, FSR_MAX_RAW);

  print_build_status();

  init_throttle();

  // core 0
  // xTaskCreatePinnedToCore(display_task_0, "display_task_0", 10000, NULL, /*priority*/ 3, NULL, /*core*/ 0);
  xTaskCreatePinnedToCore(commsStateTask_0, "commsStateTask_0", 10000, NULL, /*priority*/ 2, NULL, 0);
  // xTaskCreatePinnedToCore(flasher_task_0, "flasher_task_0", 10000, NULL, /*priority*/ 2, NULL, 0);
  xTaskCreatePinnedToCore(batteryMeasureTask_0, "batteryMeasureTask_0", 10000, NULL, /*priority*/ 1, NULL, 0);

  xDisplayChangeEventQueue = xQueueCreate(5, sizeof(uint8_t));
  xCommsStateEventQueue = xQueueCreate(3, sizeof(uint8_t));

  button0_init();
  button35_init();

  // while (!display_task_initialised)
  // {
  //   vTaskDelay(10);
  // }
}
//---------------------------------------------------------------

elapsedMillis since_sent_config_to_board;
uint8_t old_throttle;

void loop()
{
  if (since_read_trigger > READ_TRIGGER_PERIOD)
  {
    since_read_trigger = 0;

    controller_packet.throttle = throttle.get(/*enabled*/ true);
    if (old_throttle != controller_packet.throttle)
    {
      old_throttle = controller_packet.throttle;
      updateStatusPixel();
#ifdef PRINT_THROTTLE
      DEBUGVAL(controller_packet.throttle);
#endif
    }
  }

  if (since_sent_to_board > SEND_TO_BOARD_INTERVAL)
  {
    since_sent_to_board = 0;

    if (comms_state_connected == false)
    {
      controller_config.send_interval = SEND_TO_BOARD_INTERVAL;
      send_packet_to_board(CONFIG);
    }
    else
    {
      send_packet_to_board(CONTROL);
    }
  }

  nrf24.update();

  button0.loop();
  button35.loop();

  vTaskDelay(1);
}
//------------------------------------------------------------------

#endif
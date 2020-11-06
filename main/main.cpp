/*
 MapLooper - Embedded Live-Looping Tools for Digital Musical Instruments
 Copyright (C) 2020 Mathias Bredholt

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#include "Faust/Faust.h"
#include "MapLooper/MapLooper.hpp"
#include "board.h"
#include "es8388.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* TAG = "main";

extern "C" void app_main() {
  // Connect to WiFi
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  static wifi_config_t wifi_config = {
      .ap = {"MapLooper", "mappings", .authmode = WIFI_AUTH_WPA2_PSK,
             .max_connection = 4},
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  audio_board_handle_t board_handle = audio_board_init();
  audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH,
                       AUDIO_HAL_CTRL_START);
  audio_hal_set_volume(board_handle->audio_hal, 50);

  xTaskCreatePinnedToCore(
      [](void* userParam) {
        int SR = 48000;
        int BS = 64;

        Faust* faust = new Faust(SR, BS);

        // Initialize MapLooper
        MapLooper::MapLooper mapLooper;

        MapLooper::Loop* gainLoop = mapLooper.createLoop("gainLoop");
        MapLooper::Loop* cutoffLoop = mapLooper.createLoop("cutoffLoop");
        MapLooper::Loop* resonanceLoop = mapLooper.createLoop("resonanceLoop");

        gainLoop->setLength(1.0);
        cutoffLoop->setLength(1.0);
        resonanceLoop->setLength(1.0);

        gainLoop->setPulsesPerQuarterNote(48);
        cutoffLoop->setPulsesPerQuarterNote(48);
        resonanceLoop->setPulsesPerQuarterNote(48);

        // Create faust signals

        // Gain signal
        float sigMin = 0.0f, sigMax = 1.0f;

        mpr_sig_handler* sigGainHandler = [](mpr_sig sig, mpr_sig_evt evt,
                                             mpr_id inst, int length,
                                             mpr_type type, const void* value,
                                             mpr_time time) {
          Faust* faust = (Faust*)mpr_obj_get_prop_as_ptr(sig, MPR_PROP_DATA, 0);
          faust->setParamValue("gain", *(float*)value);
        };

        mpr_sig sigGain = mpr_sig_new(
            mapLooper.getDevice(), MPR_DIR_IN, "faust/gain", 1, MPR_FLT, 0,
            &sigMin, &sigMax, 0, sigGainHandler, MPR_SIG_UPDATE);

        mpr_obj_set_prop(sigGain, MPR_PROP_DATA, 0, 1, MPR_PTR, faust, 0);

        // Resonance signal
        sigMin = 0.0f, sigMax = 1.0f;

        mpr_sig_handler* sigResonanceHandler =
            [](mpr_sig sig, mpr_sig_evt evt, mpr_id inst, int length,
               mpr_type type, const void* value, mpr_time time) {
              Faust* faust =
                  (Faust*)mpr_obj_get_prop_as_ptr(sig, MPR_PROP_DATA, 0);
              faust->setParamValue("resonance", *(float*)value);
            };

        mpr_sig sigResonance = mpr_sig_new(
            mapLooper.getDevice(), MPR_DIR_IN, "faust/resonance", 1, MPR_FLT, 0,
            &sigMin, &sigMax, 0, sigResonanceHandler, MPR_SIG_UPDATE);

        mpr_obj_set_prop(sigResonance, MPR_PROP_DATA, 0, 1, MPR_PTR, faust, 0);

        // Cutoff signal
        sigMin = 50.0f;
        sigMax = 3000.0f;

        mpr_sig_handler* sigCutoffHandler = [](mpr_sig sig, mpr_sig_evt evt,
                                               mpr_id inst, int length,
                                               mpr_type type, const void* value,
                                               mpr_time time) {
          Faust* faust = (Faust*)mpr_obj_get_prop_as_ptr(sig, MPR_PROP_DATA, 0);
          faust->setParamValue("cutoffFrequency", *(float*)value);
        };

        mpr_sig sigCutoff = mpr_sig_new(
            mapLooper.getDevice(), MPR_DIR_IN, "faust/cutoffFrequency", 1,
            MPR_FLT, 0, &sigMin, &sigMax, 0, sigCutoffHandler, MPR_SIG_UPDATE);

        mpr_obj_set_prop(sigCutoff, MPR_PROP_DATA, 0, 1, MPR_PTR, faust, 0);

        // Map output of gainLoop to gain signal
        mpr_sig src;
        src = gainLoop->getOutputSignal();
        mpr_obj_push(mpr_map_new(1, &src, 1, &sigGain));

        // Map output of cutoffLoop to cutoff signal
        src = cutoffLoop->getOutputSignal();
        mpr_obj_push(mpr_map_new(1, &src, 1, &sigCutoff));

        // Map output of resonanceLoop to resonance signal
        src = resonanceLoop->getOutputSignal();
        mpr_obj_push(mpr_map_new(1, &src, 1, &sigResonance));

        // Enable recording
        float rec = 1.0;
        mpr_sig_set_value(gainLoop->getRecordSignal(), 0, 1, MPR_FLT, &rec);
        mpr_sig_set_value(cutoffLoop->getRecordSignal(), 0, 1, MPR_FLT, &rec);
        mpr_sig_set_value(resonanceLoop->getRecordSignal(), 0, 1, MPR_FLT,
                          &rec);

        // Start audio
        faust->start();

        for (;;) {
          float val = esp_random() / 4294967296.0;
          mpr_sig_set_value(gainLoop->getInputSignal(), 0, 1, MPR_FLT, &val);
          val = esp_random() / 4294967296.0;
          mpr_sig_set_value(cutoffLoop->getInputSignal(), 0, 1, MPR_FLT, &val);
          val = esp_random() / 4294967296.0;
          mpr_sig_set_value(resonanceLoop->getInputSignal(), 0, 1, MPR_FLT,
                            &val);

          if (esp_timer_get_time() > 10000000) {
            // Disable recording
            rec = 0.0;
            mpr_sig_set_value(gainLoop->getRecordSignal(), 0, 1, MPR_FLT, &rec);
            mpr_sig_set_value(cutoffLoop->getRecordSignal(), 0, 1, MPR_FLT,
                              &rec);
          }

          mapLooper.update(0);
          vTaskDelay(1);
        }
      },
      "MapLooper", 16384, nullptr, 10, nullptr, 1);
}

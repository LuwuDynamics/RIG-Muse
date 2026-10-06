# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(GADGET_SRCS
    "main.c"
    "app.c"
    "ble_server.c"
    "wifi_mgr.c"
    "wifi_known.c"
    "identity.c"
    "config_store.c"
    "vm_api.c"
    "diagnostic_log.c"
    "stack_monitor.c"
    "bug_report.c"
    "ota.c"
    "image_fetch.c"
    "../avatar/happy_anim.c"
    "pixel_font.c"
    "button.c"
    "tunnel_netif.c"
    "net_discovery.c"
    "noise_control.cpp"
    "noise_tunnel.cpp"
    "link_pairing.c"
    "pairing_signer_policy.c"
    "pairing_transcript.c"
    "factory_test.c"
)

set(GADGET_REQUIRES
    nvs_flash
    spi_flash
    esp_wifi
    esp_event
    esp_netif
    esp_http_client
    esp_https_ota
    app_update
    esp_app_format
    esp-tls
    noise_core
    product_info
    bt
    driver
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_ledc
    esp_driver_spi
    esp_driver_uart
    esp_lcd
    lwip
    mbedtls
    efuse
    muse
    camera
    # The voice board's. Requirements are resolved before the configuration
    # is loaded, so this is unconditional; unused code is not linked.
    esp_driver_i2s
    esp_driver_pcnt
)
set(RIG_BOARD_INCLUDE "")
if(CONFIG_RIG_ENABLED)
    list(APPEND GADGET_SRCS "rig_robot.c" "rig_show.c" "rig_console.c" "rig_setup.c")
    if(CONFIG_RIG_PARTICLE_FACE)
        list(APPEND GADGET_SRCS "rig_pface.c")
    endif()
    if(CONFIG_RIG_PUPPY)
        include("${CMAKE_CURRENT_LIST_DIR}/boards/rig_puppy/board.cmake")
    else()
        message(FATAL_ERROR "Enabled RIG board has no backend. Do not reuse Puppy pins/protocol.")
    endif()
endif()

if(CONFIG_RIG_HTTP_PROXY)
    list(APPEND GADGET_SRCS "proxy_connect.c" "proxy_tls.c")
endif()

# E-paper has its own status screen in place of led_status.c.
if(CONFIG_HOMEHUB_LED_BACKEND_RETERMINAL_UC8179
   OR CONFIG_HOMEHUB_LED_BACKEND_RETERMINAL_SPECTRA6)
    list(APPEND GADGET_SRCS "epaper_status.c")
else()
    list(APPEND GADGET_SRCS "led_status.c")
endif()

if(CONFIG_MUSE_ENABLED)
    list(APPEND GADGET_SRCS "muse_glue.c")
endif()

if(CONFIG_HOMEHUB_LED_BACKEND_DEVKIT_GPIO27 OR CONFIG_HOMEHUB_LED_BACKEND_VOICE_RING)
    list(APPEND GADGET_REQUIRES led_strip)
endif()

if(CONFIG_HOMEHUB_SENSECAP_SENSORS)
    list(APPEND GADGET_SRCS "sensecap_sensors.c")
endif()

if(CONFIG_RIG_VOICE)
    list(APPEND GADGET_SRCS "voice_muse_chat.c")
endif()

if(CONFIG_HOMEHUB_VOICE)
    list(APPEND GADGET_SRCS
        "voice.c"
        "voice_board.c"
        "voice_muse_chat.c"
        "voice_player.c"
    )
endif()

idf_component_register(
    SRCS ${GADGET_SRCS}
    INCLUDE_DIRS "." ${RIG_BOARD_INCLUDE}
    PRIV_INCLUDE_DIRS "../avatar"
    REQUIRES ${GADGET_REQUIRES}
)

if(CONFIG_RIG_HTTP_PROXY)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_tls_conn_new_sync")
endif()

/*
Copyright 2019 Achim Pieters | StudioPieters®

Permission is hereby granted, free of charge, to any person obtaining a copy 
of this software and associated documentation files (the "Software"), to deal 
in the Software without restriction, including without limitation the rights 
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
copies of the Software, and to permit persons to whom the Software is 
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>

#include <button.h>

// The GPIO pin that is connected to the relay on the Sonoff Dual R2
const int relay0_gpio = 12;
const int relay1_gpio = 5;
// The GPIO pin that is connected to the LED on the Sonoff Dual R2
const int led_gpio = 13;
// The GPIO pin that is connected to the button on the Sonoff Dual R2
const int BUTTON_PIN = 4;


void relay_write(int relay, bool on) {
        gpio_write(relay, on ? 1 : 0);
}

void led_write(bool on) {
        gpio_write(led_gpio, on ? 0 : 1);
}

void reset_configuration_task() {
        //Flash the LED first before we start the reset
        for (int i=0; i<3; i++) {
                led_write(true);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                led_write(false);
                vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // printf("Resetting Wifi Config\n");

        // wifi_config_reset();

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        printf("Resetting HomeKit Config\n");

        homekit_server_reset();

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        printf("Restarting\n");

        sdk_system_restart();

        vTaskDelete(NULL);
}

void reset_configuration() {
        printf("Resetting Sonoff configuration\n");
        xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);
}




int lamp_state = 3;

void top_light_on_set(homekit_value_t value);
void bottom_light_on_set(homekit_value_t value);

homekit_characteristic_t top_light_on = HOMEKIT_CHARACTERISTIC_(
        ON, true,
        .setter=top_light_on_set,
        );
homekit_characteristic_t bottom_light_on = HOMEKIT_CHARACTERISTIC_(
        ON, true,
        .setter=bottom_light_on_set,
        );

void lamp_state_set(int state) {
        lamp_state = state % 4;
        bool top_on = (state & 1) != 0;
        bool bottom_on = (state & 2) != 0;

        printf("Setting state %d, top = %s, bottom = %s\n",
               lamp_state, (top_on ? "on" : "off"), (bottom_on ? "on" : "off"));

        relay_write(relay0_gpio, top_on);
        relay_write(relay1_gpio, bottom_on);

        if (top_on != top_light_on.value.bool_value) {
                top_light_on.value = HOMEKIT_BOOL(top_on);
                homekit_characteristic_notify(&top_light_on, top_light_on.value);
        }

        if (bottom_on != bottom_light_on.value.bool_value) {
                bottom_light_on.value = HOMEKIT_BOOL(bottom_on);
                homekit_characteristic_notify(&bottom_light_on, bottom_light_on.value);
        }
}

void top_light_on_set(homekit_value_t value) {
        top_light_on.value = value;

        lamp_state_set(
                (top_light_on.value.bool_value ? 1 : 0) |
                (bottom_light_on.value.bool_value ? 2 : 0)
                );
}

void bottom_light_on_set(homekit_value_t value) {
        bottom_light_on.value = value;

        lamp_state_set(
                (top_light_on.value.bool_value ? 1 : 0) |
                (bottom_light_on.value.bool_value ? 2 : 0)
                );
}

void gpio_init() {
        gpio_enable(led_gpio, GPIO_OUTPUT);
        led_write(false);

        gpio_enable(relay0_gpio, GPIO_OUTPUT);
        gpio_enable(relay1_gpio, GPIO_OUTPUT);
        relay_write(relay0_gpio, true);
        relay_write(relay1_gpio, true);
}

void button_callback(button_event_t event, void* context) {
        lamp_state_set(lamp_state+1);
}



void lamp_identify_task(void *_args) {
        // We identify the Sonoff by turning top light on
        // and flashing with bottom light
        relay_write(relay0_gpio, true);

        for (int i=0; i<3; i++) {
                for (int j=0; j<2; j++) {
                        relay_write(relay1_gpio, true);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        relay_write(relay1_gpio, false);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                }

                vTaskDelay(250 / portTICK_PERIOD_MS);
        }

        relay_write(relay1_gpio, true);

        vTaskDelete(NULL);
}

void lamp_identify(homekit_value_t _value) {
        printf("Lamp identify\n");
        xTaskCreate(lamp_identify_task, "Lamp identify", 128, NULL, 2, NULL);
}

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, "Dual Lamp");

homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_lightbulb, .services=(homekit_service_t*[]){
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                        &name,
                        HOMEKIT_CHARACTERISTIC(MANUFACTURER, "HaPK"),
                        HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "0"),
                        HOMEKIT_CHARACTERISTIC(MODEL, "Dual Lamp"),
                        HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.1"),
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, lamp_identify),
                        NULL
                }),
                HOMEKIT_SERVICE(LIGHTBULB, .primary=true, .characteristics=(homekit_characteristic_t*[]){
                        HOMEKIT_CHARACTERISTIC(NAME, "Top Light"),
                        &top_light_on,
                        NULL
                }),
                HOMEKIT_SERVICE(LIGHTBULB, .characteristics=(homekit_characteristic_t*[]){
                        HOMEKIT_CHARACTERISTIC(NAME, "Bottom Light"),
                        &bottom_light_on,
                        NULL
                }),
                NULL
        }),
        NULL
};

homekit_server_config_t config = {
        .accessories = accessories,
        .password = "111-11-111"
};

void on_wifi_ready() {
        homekit_server_init(&config);
}

void create_accessory_name() {
        uint8_t macaddr[6];
        sdk_wifi_get_macaddr(STATION_IF, macaddr);

        int name_len = snprintf(NULL, 0, "Dual Lamp-%02X%02X%02X",
                                macaddr[3], macaddr[4], macaddr[5]);
        char *name_value = malloc(name_len+1);
        snprintf(name_value, name_len+1, "Dual Lamp-%02X%02X%02X",
                 macaddr[3], macaddr[4], macaddr[5]);

        name.value = HOMEKIT_STRING(name_value);
}

void user_init(void) {
        uart_set_baud(0, 115200);

        create_accessory_name();

        gpio_init();

        wifi_config_init("dual lamp", NULL, on_wifi_ready);

        on_wifi_ready();

        button_config_t config = BUTTON_CONFIG(button_active_low);


        int r;
        r  = button_create(BUTTON_PIN, config, button_callback, NULL);
        if (r) {
                printf("Failed to initialize a button\n");
        }
}

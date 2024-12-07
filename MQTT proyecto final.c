#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"

//*************************** Definiciones ***************************//
#define TAG "Proyecto Final"
#define WIFI_SSID "Nexxt"
#define WIFI_PASSWORD "ab123456cd"
#define WIFI_CONNECTED_BIT BIT0
#define CONFIG_BROKER_URL "mqtt://broker.hivemq.com"

// GPIO
#define SPP_BUTTON GPIO_NUM_23
#define LED0 GPIO_NUM_2

// Lógica del GPIO
#define LOGICA_NEGATIVA 0
#define LOGICA_POSITIVA 1
#define LOGICA LOGICA_NEGATIVA // Cambiar a LOGICA_POSITIVA si se requiere lógica positiva

// Estados
enum { ESTADO_0 = 0, ESTADO_1, ESTADO_2, ESTADO_3, ESTADO_4 };
uint8_t estado_actual = ESTADO_0; // Estado actual de la máquina de estado.
uint8_t estado_anterior = 99; // Se asegura que al inicio se registre un cambio.

//*************************** Variables globales ***************************//
static EventGroupHandle_t wifi_event_group;
static uint8_t spp_button_pressed = 0; // Indica si el botón físico fue presionado.
static uint8_t spp_button_mqtt = 0; // Indica si se recibió un comando desde MQTT.

//*************************** Funciones ***************************//

// Inicialización del GPIO
void inicializar_gpio(void) {
    gpio_reset_pin(SPP_BUTTON);
    gpio_set_direction(SPP_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SPP_BUTTON, GPIO_PULLUP_ONLY);

    gpio_reset_pin(LED0);
    gpio_set_direction(LED0, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "GPIO inicializado con lógica %s",
             (LOGICA == LOGICA_NEGATIVA) ? "negativa" : "positiva");
}

// Conexión Wi-Fi
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Reintentando conexión Wi-Fi...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Conexión Wi-Fi establecida, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Intentando conectar a Wi-Fi...");
}

// Manejo de eventos MQTT
static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe(event->client, "/2022-1143/SPP", 1);
            break;

        case MQTT_EVENT_DATA:
            if (strncmp(event->topic, "/2022-1143/SPP", event->topic_len) == 0) {
                if (strncmp(event->data, "1", event->data_len) == 0) {
                    spp_button_mqtt = 1;
                }
            }
            break;

        default:
            break;
    }
}

void mqtt_init(void) {
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, &mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// Máquina de estados
void maquina_estado_task(void *arg) {
    while (1) {
        if (!spp_button_pressed) {
            if (gpio_get_level(SPP_BUTTON) == LOGICA) {
                spp_button_pressed = 1;
                estado_actual = (estado_actual + 1) % 5;
            } else if (spp_button_mqtt) {
                spp_button_mqtt = 0;
                estado_actual = (estado_actual + 1) % 5;
            }
        }

        if (gpio_get_level(SPP_BUTTON) != LOGICA) {
            spp_button_pressed = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Información serial
void info_serial_task(void *arg) {
    while (1) {
        if (estado_anterior != estado_actual) {
            ESP_LOGI(TAG, "Estado actual: %d", estado_actual);
            estado_anterior = estado_actual;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Control del LED
void led_control_task(void *arg) {
    uint16_t contador = 0;
    uint16_t tiempo_barrido = 100;
    uint8_t led_level = 0;

    while (1) {
        switch (estado_actual) {
            case ESTADO_1: tiempo_barrido = 500; break;
            case ESTADO_2: tiempo_barrido = 100; break;
            case ESTADO_3: tiempo_barrido = 1000; break;
            case ESTADO_4: tiempo_barrido = (contador > 1000) ? 100 : contador + 100; break;
            default: gpio_set_level(LED0, 0); contador = 0; break;
        }

        if (contador >= tiempo_barrido) {
            contador = 0;
            led_level = !led_level;
            gpio_set_level(LED0, led_level);
        }
        contador += 10;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

//*************************** Función principal ***************************//
void app_main() {
    inicializar_gpio();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Inicializando Wi-Fi...");
    wifi_init_sta();

    ESP_LOGI(TAG, "Inicializando MQTT...");
    mqtt_init();

    xTaskCreate(maquina_estado_task, "Maquina de Estado", 2048, NULL, 5, NULL);
    xTaskCreate(info_serial_task, "Información Serial", 2048, NULL, 5, NULL);
    xTaskCreate(led_control_task, "Control del LED", 2048, NULL, 5, NULL);
}
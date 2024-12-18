#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Definiciones de estados
#define STATE_INIT 0
#define STATE_LED_OFF 1
#define STATE_LED_01 2
#define STATE_LED_02 3
#define STATE_LED_05 4
#define STATE_LED_1 5

#define TRUE 1
#define FALSE 0

#define BOTON 13
#define LED 2
#define VOLTAJE_BOTON 25

// Variables globales
int CURRENT_STATE = STATE_INIT;
int INTERVALO = 0;
int BUTTON_LAST_STATE = FALSE;

TimerHandle_t xTimer;

struct IO_VAR {
    unsigned int led_level : 1;
} io_var;

// Prototipos
void Configuracion_GPIO(void);
void ConfigurarTimer(int intervalo);
void DetenerTimer(void);
void vTimerCallback(TimerHandle_t pxTimer);

int FUNC_INIT(void);
int FUNC_LED_OFF(void);
int FUNC_LED_01(void);
int FUNC_LED_02(void);
int FUNC_LED_05(void);
int FUNC_LED_1(void);

void Configuracion_GPIO(void) {
    gpio_set_direction(BOTON, GPIO_MODE_INPUT);
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(VOLTAJE_BOTON, GPIO_MODE_OUTPUT);
    gpio_set_level(LED, FALSE);
    gpio_set_level(VOLTAJE_BOTON, TRUE);
}

void ConfigurarTimer(int intervalo) {
    if (xTimer == NULL) {
        xTimer = xTimerCreate("BlinkTimer", pdMS_TO_TICKS(intervalo), pdTRUE, NULL, vTimerCallback);
        if (xTimer != NULL) {
            xTimerStart(xTimer, 0);
        } else {
            ESP_LOGE(TAG, "Error al crear el timer");
        }
    } else {
        xTimerChangePeriod(xTimer, pdMS_TO_TICKS(intervalo), 0);
    }
}

void DetenerTimer(void) {
    if (xTimer != NULL) {
        xTimerStop(xTimer, 0);
        gpio_set_level(LED, FALSE); // Asegurar que el LED está apagado
    }
}

void vTimerCallback(TimerHandle_t pxTimer) {
    io_var.led_level = !io_var.led_level;
    gpio_set_level(LED, io_var.led_level);
}

int FUNC_INIT(void) {
    ESP_LOGI(TAG, "Estado: INIT");
    DetenerTimer();
    return STATE_LED_OFF; // Cambia al siguiente estado
}

int FUNC_LED_OFF(void) {
    ESP_LOGI(TAG, "Estado: LED OFF");
    DetenerTimer();

    for (;;) {
        if (gpio_get_level(BOTON) == TRUE && BUTTON_LAST_STATE == FALSE) {
            vTaskDelay(pdMS_TO_TICKS(50)); 
            BUTTON_LAST_STATE = TRUE;
            return STATE_LED_01;
        }
        BUTTON_LAST_STATE = gpio_get_level(BOTON);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int FUNC_LED_01(void) {
    ESP_LOGI(TAG, "Estado: LED 0.1s");
    ConfigurarTimer(100);

    for (;;) {
        if (gpio_get_level(BOTON) == TRUE && BUTTON_LAST_STATE == FALSE) {
            vTaskDelay(pdMS_TO_TICKS(50)); 
            BUTTON_LAST_STATE = TRUE;
            return STATE_LED_02;
        }
        BUTTON_LAST_STATE = gpio_get_level(BOTON);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int FUNC_LED_02(void) {
    ESP_LOGI(TAG, "Estado: LED 0.2s");
    ConfigurarTimer(200);

    for (;;) {
        if (gpio_get_level(BOTON) == TRUE && BUTTON_LAST_STATE == FALSE) {
            vTaskDelay(pdMS_TO_TICKS(50));
            BUTTON_LAST_STATE = TRUE;
            return STATE_LED_05;
        }
        BUTTON_LAST_STATE = gpio_get_level(BOTON);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int FUNC_LED_05(void) {
    ESP_LOGI(TAG, "Estado: LED 0.5s");
    ConfigurarTimer(500);

    for (;;) {
        if (gpio_get_level(BOTON) == TRUE && BUTTON_LAST_STATE == FALSE) {
            vTaskDelay(pdMS_TO_TICKS(50));
            BUTTON_LAST_STATE = TRUE;
            return STATE_LED_1;
        }
        BUTTON_LAST_STATE = gpio_get_level(BOTON);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int FUNC_LED_1(void) {
    ESP_LOGI(TAG, "Estado: LED 1s");
    ConfigurarTimer(1000);

    for (;;) {
        if (gpio_get_level(BOTON) == TRUE && BUTTON_LAST_STATE == FALSE) {
            vTaskDelay(pdMS_TO_TICKS(50));
            BUTTON_LAST_STATE = TRUE;
            return STATE_LED_OFF;
        }
        BUTTON_LAST_STATE = gpio_get_level(BOTON);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {
    Configuracion_GPIO();

    while (1) {
        switch (CURRENT_STATE) {
            case STATE_INIT:
                CURRENT_STATE = FUNC_INIT();
                break;
            case STATE_LED_OFF:
                CURRENT_STATE = FUNC_LED_OFF();
                break;
            case STATE_LED_01:
                CURRENT_STATE = FUNC_LED_01();
                break;
            case STATE_LED_02:
                CURRENT_STATE = FUNC_LED_02();
                break;
            case STATE_LED_05:
                CURRENT_STATE = FUNC_LED_05();
                break;
            case STATE_LED_1:
                CURRENT_STATE = FUNC_LED_1();
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Pequeño retraso para evitar alta carga de CPU
    }
}





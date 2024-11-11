/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "driver/gpio.h"
#include <stdlib.h>


static const char *TAG = "mqtt_example";


//Macros a utilizar para facilitar el entendimiento del código
#define ESTADO_INICIO 0
#define CERRADO 1
#define ABIERTO 2
#define CERRANDO 3
#define ABRIENDO 4
#define BUG 5
#define TRUE 1
#define FALSE 0
#define RT_MAX 12000
#define ERROR_OK 0
#define ERROR_LS 1
#define ERROR_RT 2

////GPIO DEL ESP32
#define SENSOR_ABIERTO 34   
#define SENSOR_CERRADO 35   
#define MOTOR_ABRIR 13
#define MOTOR_CERRAR 12
#define LED_ABIERTO 14
#define LED_CERRADO 27
#define LED_ERROR 26
#define LED_MQTT 2


//Inicializamos todos los estados temporales en el estado de reseteo
int NEXT_STATE    = ESTADO_INICIO;
int STATE       = ESTADO_INICIO;
int PAST_STATE     = ESTADO_INICIO;


//Estructura de datos
struct DATA_IO
{
    unsigned int LSC:1;             //Limit switch que indica que el porton está cerrado
    unsigned int LSA:1;             //Limit switch que indica que el porton está abierto
    unsigned int SPP:1;             //Comando pulso-pulso
    unsigned int MA:1;              //Salida que acciona el motor para abrir el porton
    unsigned int MC:1;              //Salida que acciona el motor para cerrar el porton
    unsigned int Cont_RT;           //Contador Run Time en segundos
    unsigned int Led_A:1;           //Led indicador del porton abriendo
    unsigned int Led_C:1;           //Led indicador del porton cerrando
    unsigned int Led_ER:1;          //Led indicador de error
    unsigned int COD_ERR;           //Código de error
    unsigned int DATOS_READY:1;     //Confirmación de recepción de los datos exteriores
}data_io;


//Prototipos de la funciones que se utilizarán en la máquina de estados
int Funcion_INICIO(void);
int Funcion_ABIERTO(void);
int Funcion_ABRIENDO(void);
int Funcion_CERRADO(void);
int Funcion_CERRANDO(void);
int Funcion_BUG(void);


//Función para configurar los GPIOs
void Configuracion_GPIO(void)
{
    gpio_set_direction(SENSOR_ABIERTO, GPIO_MODE_INPUT);
    gpio_set_direction(SENSOR_CERRADO, GPIO_MODE_INPUT);
    gpio_set_direction(MOTOR_ABRIR, GPIO_MODE_OUTPUT);
    gpio_set_direction(MOTOR_CERRAR, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_ABIERTO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_CERRADO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_ERROR, GPIO_MODE_OUTPUT);

    gpio_set_direction(LED_MQTT, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_MQTT, FALSE);
}


//Función para actualizar los valores de los GPIOs y las variables de control
void Actualización_GPIO(void)
{
    data_io.DATOS_READY = FALSE;
    vTaskDelay(10/portTICK_PERIOD_MS);
    data_io.LSA = gpio_get_level(SENSOR_ABIERTO);
    data_io.LSC = gpio_get_level(SENSOR_CERRADO);
    gpio_set_level(MOTOR_ABRIR, data_io.MA);
    gpio_set_level(MOTOR_CERRAR, data_io.MC);
    gpio_set_level(LED_ABIERTO, data_io.Led_A);
    gpio_set_level(LED_CERRADO, data_io.Led_C);
    gpio_set_level(LED_ERROR, data_io.Led_ER);
    data_io.DATOS_READY = TRUE;
}


//Función para trabajar con el dato recibido por MQTT
void Dato_MQTT(char *mensaje_recibido)
{
    if((strcmp(mensaje_recibido, "1")) == 0)
    {
        //Imprimimos en pantalla que se ha mandado a cerrar el porton
        if (STATE == ABIERTO)
        {
            printf("\nMANDATO: CERRAR EL PORTON\n");
        }

        //Imprimimos en pantalla que se ha mandado a abrir el porton
        if (STATE == CERRADO)
        {
            printf("\nMANDATO: ABRIR EL PORTON\n");
        }
        
        //Actualizamos la variable de control para abrir/cerrar el porton
        data_io.SPP = TRUE;
        mensaje_recibido = "0";
    }
}


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, "Estado_del_porton", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "Boton_de_control", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_publish(client, "Boton_de_control", "0", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
       

        /*
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        */
        
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

/////////////////////////////////////////////////////////////////////////////

        char dato_recibido [100];                               //Creamos la variable que nos almcenará el mesaje recibido por MQTT
        strncpy(dato_recibido, event->data, event->data_len);   //Copiamos el dato recibido por MQTT en la variable que creamos anteriormente
        dato_recibido [event->data_len] = '\0';                 //Aseguramos de que la cadena de texto copiada esté terminada en '\0'


        //Pasamos el mensaje copiado en la variable que creamos a la función que trabajará con el dato recibido por MQTT
        Dato_MQTT(dato_recibido);

/////////////////////////////////////////////////////////////////////////////

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());


    //Llamamos a esta función para conectarnos al broker MQTT
    mqtt_app_start();


    //Llamamos a la función para configurar los GPIOs
    Configuracion_GPIO();


    //Máquina de estado
    for(;;)
    {
        //Estado Init (Estado de reseteo)
        if (NEXT_STATE == ESTADO_INICIO)
        {
            NEXT_STATE = Funcion_INICIO();
        }

        //Estado abierto
        if (NEXT_STATE == ABIERTO)
        {
            NEXT_STATE = Funcion_ABIERTO();
        }

        //Estado abriendo
        if (NEXT_STATE == ABRIENDO)
        {
            NEXT_STATE = Funcion_ABRIENDO();
        }

        //Estado cerrado
        if (NEXT_STATE == CERRADO)
        {
            NEXT_STATE = Funcion_CERRADO();
        }

        //Estado cerrando
        if (NEXT_STATE == CERRANDO)
        {
            NEXT_STATE = Funcion_CERRANDO();
        }

        //Estado error
        if (NEXT_STATE == BUG)
        {
            NEXT_STATE = Funcion_BUG();
        }
        
    }
}


int Funcion_INICIO(void)
{
    //Actualización de los estados
    PAST_STATE = ESTADO_INICIO;
    STATE = ESTADO_INICIO;
    printf("\nESTADO ACTUAL: ESTADO INIT\n");

    //Actualización de los datos
    data_io.MA = FALSE;
    data_io.MC = FALSE;
    data_io.SPP = FALSE;
    data_io.COD_ERR = FALSE;
    data_io.Cont_RT = 0;

    //Prueba de funcionamiento de los leds
    data_io.Led_A = TRUE;
    data_io.Led_C = TRUE;
    data_io.Led_ER = TRUE;
    vTaskDelay(100/portTICK_PERIOD_MS);
    data_io.Led_A = FALSE;
    data_io.Led_C = FALSE;
    data_io.Led_ER = FALSE;

    //Actualización de los estados GPIOs y las variables de control
    Actualización_GPIO();

    //Confirmación de los datos exteriores
    while (!data_io.DATOS_READY){}

    //Loop infinito
    for(;;)
    {
        //Actualización de los estados GPIOs y las variables de control
        Actualización_GPIO();

        //Estado Init ------>> Estado cerrado
        if((data_io.LSC == TRUE) && (data_io.LSA == FALSE))
        {
            return CERRADO;
        }

        //Estado Init ------>> Estado cerrando
        if(((data_io.LSC == FALSE) && (data_io.LSA == FALSE)) || ((data_io.LSC == FALSE) && (data_io.LSA == TRUE)))
        {
            return CERRANDO;
        }

        //Estado Init ------>> Estado error
        if((data_io.LSC == TRUE) && (data_io.LSA == TRUE))
        {
            data_io.COD_ERR = ERROR_LS;
            return BUG;
        }
    }
}


int Funcion_ABIERTO(void)
{
    //Actualización de los estados
    PAST_STATE = STATE;
    STATE = ABIERTO;
    printf("\nESTADO ACTUAL: ESTADO ABIERTO\n");

    //Actualización de los datos
    data_io.MA = FALSE;
    data_io.SPP = FALSE;
    data_io.Led_A = FALSE;
    data_io.Led_C = FALSE;
    data_io.Led_ER = FALSE;

    //Actualización de los estados GPIOs y las variables de control
    Actualización_GPIO();

    //Loop infinito
    for(;;)
    {
        //Actualización de los estados GPIOs y las variables de control
        Actualización_GPIO();

        //Estado abierto ------>> Estado cerrando
        if (data_io.SPP == TRUE)
        {
            data_io.SPP = FALSE;
            return  CERRANDO;
        }
    }
}


int Funcion_ABRIENDO(void)
{
    //Actualización de los estados
    PAST_STATE = STATE;
    STATE = ABRIENDO;
    printf("\nESTADO ACTUAL: ESTADO ABRIENDO\n");

    //Actualización de los datos
    data_io.MA = TRUE;
    data_io.Cont_RT = 0;
    data_io.Led_A = TRUE;
    data_io.Led_C = FALSE;
    data_io.Led_ER = FALSE;

    //Actualización de los estados GPIOs y las variables de control
    Actualización_GPIO();

    //Tiempo de separación del porton de los limit switch
    vTaskDelay(3000/portTICK_PERIOD_MS);

    //Loop infinito
    for(;;)
    {
        //Actualización de los estados GPIOs y las variables de control
        Actualización_GPIO();

        //Estado abriendo ------>> Estado abierto
        if (data_io.LSA == TRUE)
        {
            return ABIERTO;
        }

        //Incrementamos el valor del contador RT cada 10 milisegundos
        ++data_io.Cont_RT;

        //Estado abriendo ------>> Estado error
        if (data_io.Cont_RT > RT_MAX)
        {
            data_io.COD_ERR = ERROR_RT;
            return BUG;
        }
    }
}


int Funcion_CERRADO(void)
{
    //Actualización de los estados
    PAST_STATE = STATE;
    STATE = CERRADO;
    printf("\nESTADO ACTUAL: ESTADO CERRADO\n");

    //Actualización de los datos
    data_io.MC = FALSE;
    data_io.SPP = FALSE;
    data_io.Led_A = FALSE;
    data_io.Led_C = FALSE;
    data_io.Led_ER = FALSE;

    //Actualización de los estados GPIOs y las variables de control
    Actualización_GPIO();

    //Loop infinito
    for(;;)
    {
        //Actualización de los estados GPIOs y las variables de control
        Actualización_GPIO();

        //Estado cerrado ------>> Estado abriendo
        if (data_io.SPP == TRUE)
        {
            data_io.SPP = FALSE;
            return  ABRIENDO;
        }  
    }
}


int Funcion_CERRANDO(void)
{
    //Actualización de los estados
    PAST_STATE = STATE;
    STATE = CERRANDO;
    printf("\nESTADO ACTUAL: ESTADO CERRANDO\n");

    //Actualización de los datos
    data_io.MC = TRUE;
    data_io.Cont_RT = 0;
    data_io.Led_A = FALSE;
    data_io.Led_C = TRUE;
    data_io.Led_ER = FALSE;

    //Actualización de los estados GPIOs y las variables de control
    Actualización_GPIO();

    //Tiempo de separación del porton de los limit switch
    vTaskDelay(3000/portTICK_PERIOD_MS);

    //Loop infinito
    for(;;)
    {
        //Actualización de los estados GPIOs y las variables de control
        Actualización_GPIO();

        //Estado cerrando ------>> Estado cerrado
        if (data_io.LSC == TRUE)
        {
            return CERRADO;
        }

        //Incrementamos el valor del contador RT cada 10 milisegundos
        ++data_io.Cont_RT;

        //Estado cerrando ------>> Estado error
        if (data_io.Cont_RT > RT_MAX)
        {
            data_io.COD_ERR = ERROR_RT;
            return BUG;
        } 
    }
}


int Funcion_BUG(void)
{
    //Actualización de los estados
    PAST_STATE = STATE;
    STATE = BUG;
    printf("\nESTADO ACTUAL: ESTADO ERROR\n");

    //Actualización de los datos
    data_io.MC = FALSE;
    data_io.MA = FALSE;
    data_io.SPP = FALSE;
    data_io.Led_A = FALSE;
    data_io.Led_C = FALSE;
    data_io.Led_ER = TRUE;

    //Actualización de los estados GPIOs y las variables de control
    Actualización_GPIO();

    //Mensaje indicando al usuario que hubo un error abriendo el porton
    if ((PAST_STATE == ABRIENDO) && (data_io.COD_ERR == ERROR_RT))
    {
        printf("\nERROR ABRIENDO LA PUERTA: REVISE LA POSICION DEL PORTON Y LOS LIMIT SWITCH.\n");
        printf("\nLLUEGO DE HACER LAS REVISIONES PRESIONE EL BOTON PARA RETORNAR AL FUNCIONAMIENTO NORMAL.\n");
    }

    //Mensaje indicando al usuario que hubo un error cerrando el porton
    if ((PAST_STATE == CERRANDO) && (data_io.COD_ERR == ERROR_RT))
    {
        printf("\nERROR CERRANDO LA PUERTA: REVISE LA POSICION DEL PORTON Y LOS LIMIT SWITCH.\n");
        printf("\nLUEGO DE HACER LAS REVISIONES PRESIONE EL BOTON PARA RETORNAR AL FUNCIONAMIENTO NORMAL.\n");
    }

    //Mensaje indicando al usuario que hubo un error inicializando el sistema
    if ((PAST_STATE == ESTADO_INICIO) && (data_io.COD_ERR == ERROR_LS))
    {
        printf("\nERROR INICIALIZANDO EL SISTEMA: REVISE LAS CONEXIONES DE LOS SENSORES LIMIT SWITCH.\n");
        printf("\nLUEGO DE ARREGLAR LOS SENSORES PRESIONE EL BOTON PARA REINICIAR EL SISTEMA.\n");
    }
    
    //Loop infinito
    for(;;)
    {
        //Actualización de los estados GPIOs y las variables de control
        Actualización_GPIO();

        //Estado error ------>> Estado abriendo
        if ((PAST_STATE == ABRIENDO) && (data_io.SPP == TRUE))
        {
            data_io.SPP = FALSE;
            printf("\nDEVUELTA AL FUNCIONAMIENTO PARA ABRIR LA PUERTA\n");
            data_io.COD_ERR = ERROR_OK;
            return ABRIENDO;
        }

        //Estado error ------>> Estado cerrando
        if ((PAST_STATE == CERRANDO) && (data_io.SPP == TRUE))
        {
            data_io.SPP = FALSE;
            printf("\nDEVUELTA AL FUNCIONAMIENTO PARA CERRAR LA PUERTA\n");
            data_io.COD_ERR = ERROR_OK;
            return CERRANDO;
        }

        //Estado error ------>> Estado Init
        if ((PAST_STATE == ESTADO_INICIO) && (data_io.SPP == TRUE))
        {
            data_io.SPP = FALSE;
            printf("\nDEVUELTA AL FUNCIONAMIENTO PARA REINICIAR EL SISTEMA\n");
            data_io.COD_ERR = ERROR_OK;
            return ESTADO_INICIO;
        }
    }
}
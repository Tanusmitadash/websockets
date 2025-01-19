/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"

#define WIFI_SSID "Upeya-2-4"
#define WIFI_PASS "asdfghjkl"
//#define CONFIG_WEBSOCKET_URI "ws://192.168.1.31:8123/api/websocket" // Update IP and endpoint

static void websocket_app_start(void);
static void wifi_init(void);

#define NO_DATA_TIMEOUT_SEC 5
//#define HA_WEBSOCKET_URL "ws://your_homeassistant_url/api/websocket"
//#define HA_WEBSOCKET_TOKEN eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiI0ODc2ZDgxMWYyNjM0NWJkOGMwNGI1OWFkMjcyMjQwMCIsImlhdCI6MTczMjU5Njc1MCwiZXhwIjoyMDQ3OTU2NzUwfQ.jmCmRrnzB7TtrJEZjaOx_jFNKBB_MchitIX0u7cmZTg


static const char *TAG = "esp-tls";

static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;

static void shutdown_signaler(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
    xSemaphoreGive(shutdown_sema);
}

#if CONFIG_WEBSOCKET_URI_FROM_STDIN
static void get_string(char *line, size_t size)
{
    int count = 0;
    while (count < size) {
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
}

#endif /* CONFIG_WEBSOCKET_URI_FROM_STDIN */

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        break;
    
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "Received WebSocket Data: %.*s", data->data_len, (char *)data->data_ptr);   //change-1
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        if (data->op_code == 0x08 && data->data_len == 2) {
            ESP_LOGW(TAG, "Received closed message with code=%d", 256*data->data_ptr[0] + data->data_ptr[1]);
        } else {
            ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
        }
        ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);

        xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        break;
        //change-2
    default:
        ESP_LOGW(TAG, "Unknown WebSocket Event ID: %d", event_id);
        break;

    }
}

static void websocket_app_start(void)
{
    esp_websocket_client_config_t websocket_cfg = {};

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();

#if CONFIG_WEBSOCKET_URI_FROM_STDIN
    char line[128];

    ESP_LOGI(TAG, "Please enter uri of websocket endpoint");
    get_string(line, sizeof(line));

    websocket_cfg.uri = line;
    ESP_LOGI(TAG, "Endpoint uri: %s\n", line);

#else
    websocket_cfg.uri = CONFIG_WEBSOCKET_URI;
   // websocket_cfg.transport = WEBSOCKET_TRANSPORT_OVER_TCP;
//eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIzZDJiNDRkMDc4MzE0MzQyYjZlOWMwNDliYWJjYWE3YiIsImlhdCI6MTczNTEyMzU3NSwiZXhwIjoyMDUwNDgzNTc1fQ.YZ1KuA52NanX6Bvmr9zDQAgDCXV-E8Yot0yuqr583N8
//eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJlNzA5ZWI2ZmVhZmQ0MjcyOGU4MGFiMDBiYzNmOWExMyIsImlhdCI6MTczNjQwMjUxNSwiZXhwIjoyMDUxNzYyNTE1fQ.3vJMvNgfrbgn7tC0Y55wKG168QTnYb3xn1ISUPxiyLs
#endif /* CONFIG_WEBSOCKET_URI_FROM_STDIN */

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    esp_websocket_client_start(client);
    xTimerStart(shutdown_signal_timer, portMAX_DELAY);

    // Send authentication message to Home Assistant // Step 1: Authenticate
   
    char auth_message[] = "{\"type\":\"auth\",\"access_token\":\"\"}";
  /*  {
  "type": "auth",
  "access_token": "{{token}}"
}*/
    ESP_LOGI(TAG, "Sending authentication message...");
    esp_websocket_client_send_text(client, auth_message, strlen(auth_message), portMAX_DELAY);
    vTaskDelay(2000 / portTICK_RATE_MS); 

 /* Loop for sending messages
    for (int i = 0; i < 10; i++) {
        char message[128];
        snprintf(message, sizeof(message), "{\"type\":\"ping\",\"id\":%d}", i);
        esp_websocket_client_send_text(client, message, strlen(message), portMAX_DELAY);
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Delay between messages
    }*/
    
 char data[256];
    int i=0;
    while(i<10)
    {
    if (esp_websocket_client_is_connected(client)){
    //int len = sprintf(data, "hello %04d", i++);
   // snprintf(data, sizeof(data), "{\"type\":\"ping\",\"id\":%d}", i);
   const char *entity_id = "switch.6switch1fan_3_switch_5"; 

    
  /* snprintf(data, sizeof(data),
             "{"id": 1 %d,"type":"call_service","domain":"switch,"service":"turn_off","service_data":{"switch.6switch1fan_switch":"switch.6switch1fan_switch%s"}}",
             i++, entity_id);
*/
 //int id = 1; 
 snprintf(data, sizeof(data),
             "{\"id\":1 ,\"type\":\"call_service\",\"domain\":\"switch\",\"service\":\"turn_on\",\"target\":{\"entity_id\":[\"switch.6switch1fan_3_switch_5\"]}}");

   /* snprintf(data, sizeof(data),
    "{\"id\":1%d,\"type\":\"call_service\",\"domain\":\"switch\",\"service\":\"turn_off\",\"service_data\":{\"switch.6switch1fan_switch":"switch.6switch1fan_switch"}}",
    i++, entity_id, entity_id);*/
         

    ESP_LOGI(TAG, "Sending %s", data);
            esp_websocket_client_send_text(client, data, strlen(data) ,portMAX_DELAY);
        }
        vTaskDelay(5000 / portTICK_RATE_MS);

    }
    


   /*char data[32];
    int i = 0;
    while (i < 5) {
        if (esp_websocket_client_is_connected(client)) {
            int len = sprintf(data, "hello %04d", i++);
            ESP_LOGI(TAG, "Sending %s", data);
            esp_websocket_client_send_text(client, data, len, portMAX_DELAY);
        }
        vTaskDelay(1000 / portTICK_RATE_MS);
    }*/

    xSemaphoreTake(shutdown_sema, portMAX_DELAY);
    //esp_websocket_client_stop(client);
    //esp_websocket_client_destroy(client);
    esp_websocket_client_close(client, portMAX_DELAY);
    ESP_LOGI(TAG, "Websocket Stopped");
    esp_websocket_client_destroy(client);
}



/* Wi-Fi Initialization */

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying connection to Wi-Fi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Connected to Wi-Fi");
    }
}

static void wifi_init(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_any_id);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}


void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("WEBSOCKET_CLIENT", ESP_LOG_DEBUG);
    esp_log_level_set("TRANSPORT_WS", ESP_LOG_DEBUG);
    esp_log_level_set("TRANS_TCP", ESP_LOG_DEBUG);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
     

    websocket_app_start();
}




   

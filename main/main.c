#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

#include "driver/gpio.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY 10

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

extern const char server_cert_pem_start[] asm("_binary_domain_pem_start");
extern const char server_cert_pem_end[] asm("_binary_domain_pem_end");

static const char *TAG_WIFI = "WIFI";
static const char *TAG_HTTP = "HTTP";
static const char *TAG_DOWNLOAD = "DOWNLOAD_FW";
static const char *TAG_UPDATE = "UPDATE_SW";
static const char *TAG_NVS = "NVS";

static int s_retry_num = 0;

/**
 * @brief HTTP event handler
 * 
 * @param evt HTTP client event
 * @return esp_err_t 
 */
esp_err_t _http_event_handler(esp_http_client_event_t *evt);

/**
 * @brief Event handler for WiFi and IP events
 * 
 * @param arg User-defined argument
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG_WIFI, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG_WIFI, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG_WIFI, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Initialize WiFi in station mode
 */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Set hostname for station interface
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_set_hostname(netif, "Hazem_IAP");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Set WiFi SSID and password
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "realme 6 Pro",
            .password = "01015548853",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG_WIFI, "connected to ap SSID:%s password:%s",
                 "realme 6 Pro", "01015548853");
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG_WIFI, "Failed to connect to SSID:%s, password:%s",
                 "realme 6 Pro", "01015548853");
    }
    else
    {
        ESP_LOGE(TAG_WIFI, "UNEXPECTED EVENT");
    }
}

static double FWVersion;
// receive buffer
char rcv_buffer[200];
static SemaphoreHandle_t xSemaphore = NULL;

/**
 * @brief Download and log file from the given URL
 * 
 * @param url URL of the file to download
 * @param FWVersion Pointer to the firmware version
 * @param binFilePath Path to the binary file
 */
void download_and_log_file(const char *url, double *FWVersion, char *binFilePath)
{
    ESP_LOGW(TAG_HTTP, "Start Downloading File");
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .cert_pem = server_cert_pem_start,
        .cert_len = server_cert_pem_end - server_cert_pem_start
    };
    xSemaphore = xSemaphoreCreateBinary();
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG_HTTP, "Error waiting for semaphore");
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_HTTP, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG_HTTP, "received buffer = '%s' ,len = %d", rcv_buffer, strlen(rcv_buffer));
        cJSON *json = cJSON_Parse(rcv_buffer);
        if (json == NULL)
            ESP_LOGE(TAG_DOWNLOAD, "downloaded file is not a valid json, aborting...");
        else
        {
            cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
            cJSON *file = cJSON_GetObjectItemCaseSensitive(json, "binfile");
            if (!cJSON_IsNumber(version))
            {
                ESP_LOGE(TAG_DOWNLOAD, "unable to read new version, aborting...");
            }
            else
            {
                ESP_LOGI(TAG_DOWNLOAD, "current FW version = %0.11f, server FW version = %0.11f", *FWVersion, version->valuedouble);
                if (*FWVersion != version->valuedouble)
                {
                    *FWVersion = version->valuedouble;
                    nvs_handle_t nvs_handle;
                    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG_NVS, "Error opening NVS handle %i",err);
                    }
                    err = nvs_set_u64(nvs_handle, "FWVersion", *(uint64_t*)FWVersion);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG_NVS, "Error set FWVersion %i",err);
                    }
                    err = nvs_commit(nvs_handle);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG_NVS, "Error commit FWVersion %i",err);
                    }
                    nvs_close(nvs_handle);

                    if (cJSON_IsString(file) && (file->valuestring != NULL))
                    {
                        strcpy(binFilePath, file->valuestring);
                        ESP_LOGI(TAG_DOWNLOAD, "downloading and installing new firmware (%s)...", binFilePath);

                        config.url = binFilePath;
                        config.event_handler = NULL;
                        esp_https_ota_config_t ota_client_config = {
                            .http_config = &config,
                        };
                        esp_err_t ret = esp_https_ota(&ota_client_config);
                        if (ret == ESP_OK)
                        {
                            ESP_LOGI(TAG_UPDATE, "OTA OK, restarting...");
                            esp_restart();
                        }
                        else
                        {
                            ESP_LOGE(TAG_UPDATE, "OTA failed...");
                        }
                    }
                    else
                    {
                        ESP_LOGE(TAG_DOWNLOAD, "unable to read the new file name, aborting...");
                    }
                }
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    // Initialize NVS (Non-Volatile Storage)
    ESP_LOGE(TAG_NVS, "Brand New FW");
    esp_err_t ret = nvs_flash_init();
    // If no free pages or new version found, erase NVS partition and reinitialize
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGE(TAG_NVS, "Error NO_FREE_PAGES || NEW_VERSION_FOUND");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // read the saved version
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error opening NVS handle %i",err);
    }

    // Read the binary data
    // size_t size = sizeof(double);
    err = nvs_get_u64(nvs_handle, "FWVersion", (uint64_t*)&FWVersion);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error(%i) getting value create it with FWVersion = 0",err);
        FWVersion = 0;
        nvs_set_u64(nvs_handle, "FWVersion", *(uint64_t*)&FWVersion);
        nvs_commit(nvs_handle);
    }
    else
    {
        ESP_LOGW(TAG_NVS, "FW Version %0.11f", FWVersion);
    }

    // Close the NVS handle
    nvs_close(nvs_handle);

    // load master server version

    // Log the WiFi mode
    ESP_LOGI(TAG_WIFI, "ESP_WIFI_MODE_STA");
    // Initialize the WiFi station
    wifi_init_sta();

    char filePath[200] = {0};
    download_and_log_file("https://raw.githubusercontent.com/hazem3443/OTA/IAP_OTA_WORKFLOW/Firmware.json", &FWVersion, filePath);
}

// esp_http_client event handler
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        break;
    case HTTP_EVENT_ON_CONNECTED:
        break;
    case HTTP_EVENT_HEADER_SENT:
        break;
    case HTTP_EVENT_ON_HEADER:
        break;
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            strncpy(rcv_buffer, (char *)evt->data, evt->data_len);
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        xSemaphoreGive(xSemaphore);
        break;
    case HTTP_EVENT_DISCONNECTED:
        break;
    default:
        break;
    }
    ESP_LOGI(TAG_HTTP, "_http_event_handler %i", evt->event_id);
    return ESP_OK;
}

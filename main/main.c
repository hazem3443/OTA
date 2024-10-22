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
/*
#define WEB_SERVER "example.com"
#define WEB_PORT "80"
#define WEB_PATH "/"

static const char *REQUEST = "GET " WEB_PATH " HTTP/1.0\r\n"
                             "Host: " WEB_SERVER ":" WEB_PORT "\r\n"
                             "User-Agent: esp-idf/1.0 esp32\r\n"
                             "\r\n";

static void http_get_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while (1)
    {
        int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

        if (err != 0 || res == NULL)
        {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Code to print the resolved IP.Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if (s < 0)
        {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if (connect(s, res->ai_addr, res->ai_addrlen) != 0)
        {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, REQUEST, strlen(REQUEST)) < 0)
        {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                       sizeof(receiving_timeout)) < 0)
        {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        // Read HTTP response
        do
        {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf) - 1);
            for (int i = 0; i < r; i++)
            {
                putchar(recv_buf[i]);
            }
        } while (r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);
        for (int countdown = 10; countdown >= 0; countdown--)
        {
            ESP_LOGI(TAG, "%d... ", countdown);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}

Main function
void log_task(void *pvParameters)
{
    while(1) {
        ESP_LOGI(TAG, "This log is added to test the periodicity of 100ms task. Timestamp: %lld", esp_timer_get_time());
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
*/
#include "esp_heap_caps.h"
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
                            // .partial_http_download = true,
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
    /*
            int content_length = esp_http_client_fetch_headers(client);
            ESP_LOGW(TAG, "downloaded File Size %d", content_length);

            char *buffer = malloc(content_length);
            if (buffer == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for download buffer");
            }
            else
            {
                int total_read_len = 0, read_len;
                while (total_read_len < content_length)
                {
                    read_len = esp_http_client_read(client, buffer + total_read_len, content_length - total_read_len);
                    if (read_len <= 0)
                    {
                        ESP_LOGE(TAG, "Error reading data");
                        break;
                    }
                    total_read_len += read_len;
                }

                // Store the downloaded data in NVS
                nvs_handle_t nvs_handle;
                err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Error opening NVS");
                }
                else
                {
                    err = nvs_set_str(nvs_handle, "downloaded_file", buffer);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Error writing to NVS");
                    }
                    nvs_close(nvs_handle);
                }

                free(buffer);
            }
        }
        */
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

    // Create a new task for the HTTP GET request
    // xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);

    // Create a new task for logging every 100ms
    // xTaskCreate(&log_task, "log_task", 4096, NULL, 5, NULL);

    // Call the new function with the URL of the 'New_SW.txt' file in the GitHub repo

    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    ESP_LOGW(TAG_NVS, "Total entries: %d", nvs_stats.total_entries);
    ESP_LOGW(TAG_NVS, "Used entries: %d", nvs_stats.used_entries);
    ESP_LOGW(TAG_NVS, "Free entries: %d", nvs_stats.free_entries);
    ESP_LOGW(TAG_NVS, "Namespace count: %d", nvs_stats.namespace_count);

    // start the check update task
    // xTaskCreate(&check_update_task, "check_update_task", 8192, NULL, 5, NULL);
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
/*
void check_update_task(void *pvParameter)
{
    while (1)
    {
        printf("Looking for a new firmware...");
        // configure the esp_http_client
        esp_http_client_config_t config = {
            .url = UPDATE_JSON_URL,
            .event_handler = _http_event_handler,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);

        // downloading the json file
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK)
        {

            // parse the json file
            cJSON *json = cJSON_Parse(rcv_buffer);
            if (json == NULL)
                printf("downloaded file is not a valid json, aborting...");
            else
            {
                cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
                cJSON *file = cJSON_GetObjectItemCaseSensitive(json, "binfile");

                // check the version
                if (!cJSON_IsNumber(version))
                    printf("unable to read new version, aborting...");
                else
                {
                    double new_version = version->valuedouble;
                    if (new_version > FIRMWARE_VERSION)
                    {
                        printf("current firmware version (%.1f) is lower than the available one (%.1f), upgrading...", FIRMWARE_VERSION, new_version);
                        if (cJSON_IsString(file) && (file->valuestring != NULL))
                        {
                            printf("downloading and installing new firmware (%s)...", file->valuestring);

                            esp_http_client_config_t ota_client_config = {
                                .url = file->valuestring,
                                .cert_pem = server_cert_pem_start,
                            };
                            esp_err_t ret = esp_https_ota(&ota_client_config);
                            if (ret == ESP_OK)
                            {
                                printf("OTA OK, restarting...");
                                esp_restart();
                            }
                            else
                            {
                                printf("OTA failed...");
                            }
                        }
                        else
                            printf("unable to read the new file name, aborting...");
                    }
                    else
                        printf("current firmware version (%.1f) is greater or equal to the available one (%.1f), nothing to do...", FIRMWARE_VERSION, new_version);
                }
            }
        }
        else
            printf("unable to download the json file, aborting...");

        // cleanup
        esp_http_client_cleanup(client);

        printf("");
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}
*/

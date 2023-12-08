/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_private/wifi.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "../../common/csi.h"

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN
#define EXAMPLE_DB_HOST_MAC        CONFIG_DB_HOST_MAC
#define EXAMPLE_DB_HOST_IP         CONFIG_DB_HOST_IP

#define UDP_PORT 4950
#define UDP_PORT_HOST 4951

QueueHandle_t queue;

const char *TAG = "csi_testbed_softAP";

static int connected_stations()
{
    wifi_sta_list_t list;
    esp_wifi_ap_get_sta_list(&list);

    for (int i = 0; i < list.num; i++) {
        char sta_mac[20] = {0};
        sprintf(sta_mac, MACSTR, MAC2STR(list.sta[i].mac));

        if (strcasecmp(sta_mac, EXAMPLE_DB_HOST_MAC)) {
            continue;
        }

        return list.num > 1 ? 1 : 0;
    }

    return 0;
}

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || !info->mac) {
        ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    char src_mac[20] = {0};
    sprintf(src_mac, MACSTR, MAC2STR(info->mac));

    wifi_sta_list_t list;
    esp_wifi_ap_get_sta_list(&list);

    uint8_t store_csi = 0;

    for (int i = 0; i < list.num; i++) {
        char sta_mac[20] = {0};
        sprintf(sta_mac, MACSTR, MAC2STR(list.sta[i].mac));

        if (strcasecmp(sta_mac, src_mac)) {
            continue;
        }

        if (!strcasecmp(sta_mac, EXAMPLE_DB_HOST_MAC)) {
            continue;
        }

        store_csi = 1;
    }

    if (!store_csi) {
        return;
    }

//    if (strcmp(src_mac, "c8:f0:9e:4d:0e:cc") && strcmp(src_mac, "c8:f0:9e:50:7b:c0")) {
//        return;
//    }

    static uint32_t s_count = 0;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

    if (!s_count) {
        ESP_LOGI(TAG, "================ CSI RECV ================");
        ets_printf("type,seq,mac,node,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,len,first_word,data\n");
    }

    /** Only LLTF sub-carriers are selected. */
    info->len = 128;

    printf("CSI_DATA,%lu," MACSTR "," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
            s_count++, MAC2STR(info->mac), MAC2STR(info->dmac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
            rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
            rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
            rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
            rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state);

    printf(",%d,%d,[%d", info->len, info->first_word_invalid, info->buf[0]);

    for (int i = 1; i < info->len; i++) {
        printf(" %d", info->buf[i]);
    }

    printf("]\n");

    // copy data to new struct
    static_wifi_csi_info_t csi;

    csi.x = *info;
    csi.x.buf = NULL;
    memcpy(csi.buf, info->buf, info->len);
    csi.len = info->len;

    if(xQueueSendToBack(queue, (void *)&csi, (TickType_t)0) != pdPASS) {
        ESP_LOGD(TAG, "queue error - full?");
    }
}

static void wifi_csi_init()
{
    /**
     * @brief In order to ensure the compatibility of routers, only LLTF sub-carriers are selected.
     */
    wifi_csi_config_t csi_config = {
            .lltf_en           = true,
            .htltf_en          = false,
            .stbc_htltf2_en    = false,
            .ltf_merge_en      = true,
            .channel_filter_en = true,
            .manu_scale        = true,
            .shift             = true,
    };

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

//    printf("11b: %d\n", s_ap_info.phy_11b);
//    printf("11g: %d\n", s_ap_info.phy_11g);
//    printf("11n: %d\n", s_ap_info.phy_11n);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    //ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));
    //ESP_ERROR_CHECK(esp_wifi_config_11b_rate(WIFI_IF_AP, true));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_internal_set_fix_rate(WIFI_IF_AP, 1, WIFI_PHY_RATE_MCS0_LGI));

    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

char *data = (char *) "1\n";

void socket_transmitter_sta_loop(int (*connected_stations)()) {
    queue = xQueueCreate(50, sizeof(static_wifi_csi_info_t));
    int socket_fd = -1, socket_fd_host = -1;
    while (1) {


        close(socket_fd);
        close(socket_fd_host);

        char *ip = (char *) "225.1.1.1";
        struct sockaddr_in caddr;
        caddr.sin_family = AF_INET;
        caddr.sin_port = htons(UDP_PORT);

        struct sockaddr_in host_addr;

        host_addr.sin_family = AF_INET;
        host_addr.sin_port = htons(UDP_PORT_HOST);

        if (inet_aton(EXAMPLE_DB_HOST_IP, &host_addr.sin_addr) == 0) {
            printf("ERROR: inet_aton\n");
            abort();
        }

        socket_fd_host = socket(PF_INET, SOCK_DGRAM, 0);
        if (socket_fd_host == -1) {
            printf("ERROR: Socket creation error [%s]\n", strerror(errno));
            abort();
        }

        while (!connected_stations()) {
            // wait until a station connects
            printf("no stations/server connected, waiting...\n");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        printf("at least one station is connected\n");
        if (inet_aton(ip, &caddr.sin_addr) == 0) {
            printf("ERROR: inet_aton\n");
            continue;
        }

        socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
        if (socket_fd == -1) {
            printf("ERROR: Socket creation error [%s]\n", strerror(errno));
            continue;
        }

        uint8_t dnode[6];
        ESP_ERROR_CHECK(esp_read_mac(dnode, ESP_MAC_WIFI_SOFTAP));

        ESP_LOGI(TAG, "sending multicast frames.");
        // double lag = 0.0;
        while (1) {
            printf("checking connected stations....\n");
            // double start_time = get_steady_clock_timestamp();
            if (!connected_stations()) {
                printf("ERROR: no connected stations\n");
                break;
            }

            printf("start sending...\n");
            if (sendto(socket_fd, &data, strlen(data), 0, (const struct sockaddr *) &caddr, sizeof(caddr)) !=
                strlen(data)) {
                vTaskDelay(1);
                continue;
            }

            static_wifi_csi_info_t csi_info;

            while (xQueueReceive(queue, &csi_info, ( TickType_t ) 0)) {
                printf("received something from the queue\n");
                char buff_json[800] = {0};
                int len_json = csi_to_json(&csi_info, (char *)buff_json, sizeof(buff_json), &dnode);

                if(len_json < 0)
                    continue;

                int to_send = len_json, sent = 0, retires = 0;
                while (to_send > 0 && retires < 5) {
                    printf("sending to db host\n");
                    if ((sent = sendto(socket_fd_host, buff_json + sent, to_send, 0, (const struct sockaddr *) &host_addr, sizeof(host_addr))) == -1) {
                        ESP_LOGE(TAG, "sendto: %d %s", socket_fd_host, strerror(errno));
                        retires++;
                        vTaskDelay(2);
                        //abort();
                    } else {
                        to_send -= sent;
                    }
                }

                printf("\n%s\n", buff_json);
            }

//#if defined CONFIG_PACKET_RATE && (CONFIG_PACKET_RATE > 0)
//            double wait_duration = (1000.0 / CONFIG_PACKET_RATE) - lag;
//            int w = floor(wait_duration);
//            vTaskDelay(w);
//#else
            vTaskDelay(2);
//#endif
            // double end_time = get_steady_clock_timestamp();
            // lag = end_time - start_time;
        }
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
    wifi_csi_init();
    //xTaskCreatePinnedToCore(socket_transmitter_sta_loop, "socket_transmitter_sta_loop", 10000, (void *) &connected_stations, 5, NULL, 0);
    //xTaskCreate(csi_task, "csi_task", 10000, NULL, 6, NULL);
    xTaskCreate(socket_transmitter_sta_loop, "socket_transmitter_sta_loop", 10000, (void *) &connected_stations, 5, NULL);
}

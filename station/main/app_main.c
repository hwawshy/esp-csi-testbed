/* Get recv router csi

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "nvs_flash.h"

#include "esp_mac.h"
#include "esp_system.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_private/wifi.h"
//#include "esp_now.h"

#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"

#include "wifi.h"
#include "../../common/csi.h"

//#include "protocol_examples_common.h"

#define CONFIG_SEND_FREQUENCY      100

#define UDP_PORT 4950
#define UDP_PORT_HOST 4951
#define UDP_BUFF_LEN 100

const char *TAG = "csi_testbed_station";

QueueHandle_t queue;

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf || !info->mac) {
        ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    if (memcmp(info->mac, ctx, 6)) {
        return;
    }

    char dst_mac[20] = {0};
    sprintf(dst_mac, MACSTR, MAC2STR(info->dmac));

    if (strcmp(dst_mac, "01:00:5e:01:01:01")) {
        return;
    }

    uint8_t dmac[6];
    ESP_ERROR_CHECK(esp_read_mac(dmac, ESP_MAC_WIFI_STA));

    static uint32_t s_count = 0;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

    if (!s_count) {
        ESP_LOGI(TAG, "================ CSI RECV ================");
        ets_printf("type,seq,mac,node,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,len,first_word,data\n");
    }

    /** Only LLTF sub-carriers are selected. */
    info->len = 128;

    printf("CSI_DATA,%lu," MACSTR "," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
            s_count++, MAC2STR(info->mac), MAC2STR(dmac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
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

    static wifi_ap_record_t s_ap_info = {0};
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&s_ap_info));
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, s_ap_info.bssid));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

//    printf("11b: %d\n", s_ap_info.phy_11b);
//    printf("11g: %d\n", s_ap_info.phy_11g);
//    printf("11n: %d\n", s_ap_info.phy_11n);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static void csi_task(void *pvParameters)
{
    queue = xQueueCreate(50, sizeof(static_wifi_csi_info_t));


    int socket_fd, socket_fd_host;
    struct sockaddr_in my_addr, host_addr;
    struct sockaddr_storage their_addr;
    struct ip_mreq group;
    char buf[UDP_BUFF_LEN];
    char s[INET_ADDRSTRLEN];
    int numbytes;
    socklen_t addr_len;

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(UDP_PORT);
    my_addr.sin_addr.s_addr  = INADDR_ANY;

    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(UDP_PORT_HOST);

    char *ip_host = (char *) "192.168.4.11";

    if (inet_aton(ip_host, &host_addr.sin_addr) == 0) {
        printf("ERROR: inet_aton\n");
        abort();
    }

    uint8_t dnode[6];
    ESP_ERROR_CHECK(esp_read_mac(dnode, ESP_MAC_WIFI_STA));

    while (1) {
        esp_netif_ip_info_t local_ip;
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &local_ip);

        socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
        socket_fd_host = socket(PF_INET, SOCK_DGRAM, 0);
        if (socket_fd == -1 || socket_fd_host == -1) {
            printf("ERROR: Socket creation error [%s]\n", strerror(errno));
            abort();
        }

        int reuse = 1;

//    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,(char *)&reuse, sizeof(reuse)) < 0) {
//        printf("ERROR: SO_REUSEADDR [%s]\n", strerror(errno));
//        close(socket_fd);
//        abort();
//    }

        if (bind(socket_fd, (struct sockaddr *)&my_addr, sizeof my_addr)) {
            close(socket_fd);
            printf("ERROR: bind error [%s]\n", strerror(errno));
        }

        group.imr_multiaddr.s_addr = inet_addr("225.1.1.1");
        inet_addr_from_ip4addr(&group.imr_interface, &local_ip.ip);

        if (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,(char *)&group, sizeof(group)) < 0) {
            perror("adding multicast group");
            close(socket_fd);
            exit(1);
        }

        while (1) {

            addr_len = sizeof their_addr;
            if ((numbytes = recvfrom(socket_fd, buf, UDP_BUFF_LEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
                close(socket_fd);
                printf("ERROR: recvfrom error [%s]\n", strerror(errno));
                break;
            }

            static_wifi_csi_info_t csi_info;


            while (xQueueReceive(queue, &csi_info, ( TickType_t ) 0)) {
                char buff_json[3000] = {0};
                int len_json = csi_to_json(&csi_info, (char *)buff_json, sizeof(buff_json), &dnode);

                if(len_json < 0)
                    continue;

                int to_send = len_json, sent = 0;
                while (to_send > 0) {
                    if ((sent = sendto(socket_fd_host, buff_json + sent, to_send, 0, (const struct sockaddr *) &host_addr, sizeof(host_addr))) == -1) {
                        ESP_LOGE(TAG, "sendto: %d %s", socket_fd_host, strerror(errno));
                        abort();
                    }

                    to_send -= sent;
                }

                printf("\n%s\n", buff_json);
            }

//            printf("listener: got packet from %s\n",
//                   inet_ntop(their_addr.ss_family,
//                             get_in_addr((struct sockaddr *)&their_addr),
//                             s, sizeof s));
//            printf("listener: packet is %d bytes long\n", numbytes);
//            buf[numbytes] = '\0';
//            printf("listener: packet contains \"%s\"\n", buf);
        }

        close(socket_fd);
        close(socket_fd_host);
    }

    vTaskDelete(NULL);
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK(esp_event_loop_create_default());

    /**
     * @brief This helper function configures Wi-Fi, as selected in menuconfig.
     *        Read "Establishing Wi-Fi Connection" section in esp-idf/examples/protocols/README.md
     *        for more information about this function.
     */
    //ESP_ERROR_CHECK(example_connect());
    wifi_init_sta();
    wifi_csi_init();
    xTaskCreate(csi_task, "csi_task", 10000, NULL, 5, NULL);
}

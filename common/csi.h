#include "mbedtls/base64.h"

// code adapted from: https://github.com/roger-/esp32-csi-server

#define buff_printf(...) assert(len_json < len_buff_json); len_json += snprintf(buff_json + len_json, len_buff_json - len_json, __VA_ARGS__)
#define boolstr(expr) ((expr ? "true" : "false"))

typedef struct {
    wifi_csi_info_t x;
    int8_t buf[128];
    uint16_t len;
} static_wifi_csi_info_t;

extern const char *TAG;

int csi_to_json(static_wifi_csi_info_t *csi_info, char *buff_json, int len_buff_json, uint8_t (* dnode)[6])
{
    int retval;
    int len_json = 0;

    buff_printf("{");
    buff_printf(
            "\"mac\": \"" MACSTR "\","
                                 "\"dmac\": \"" MACSTR "\","
                                 "\"dnode\": \"" MACSTR "\","
                                   "\"len\": %d,"
                                   "\"first_word_invalid\": %s",
            MAC2STR(csi_info->x.mac),
            MAC2STR(csi_info->x.dmac),
            MAC2STR(*dnode),
            csi_info->x.len,
            boolstr(csi_info->x.first_word_invalid));

    buff_printf(
            ",\"rssi\": %d,"
            "\"rate\": %d,"
            "\"sig_mode\": %d,"
            "\"mcs\": %d,"
            "\"cwb\": %d,"
            "\"smoothing\": %d,"
            "\"not_sounding\": %d,"
            "\"aggregation\": %d,"
            "\"stbc\": %d,"
            "\"fec_coding\": %d,"
            "\"sgi\": %d,"
            "\"noise_floor\": %.2f,"
            "\"ampdu_cnt\": %d,"
            "\"channel\": %d,"
            "\"secondary_channel\": %d,"
            "\"timestamp\": %.6f,"
            "\"ant\": %d,"
            "\"sig_len\": %d,"
            "\"rx_state\": %d",
            csi_info->x.rx_ctrl.rssi,
            csi_info->x.rx_ctrl.rate,
            csi_info->x.rx_ctrl.sig_mode,
            csi_info->x.rx_ctrl.mcs,
            csi_info->x.rx_ctrl.cwb,
            csi_info->x.rx_ctrl.smoothing,
            csi_info->x.rx_ctrl.not_sounding,
            csi_info->x.rx_ctrl.aggregation,
            csi_info->x.rx_ctrl.stbc,
            csi_info->x.rx_ctrl.fec_coding,
            csi_info->x.rx_ctrl.sgi,
            csi_info->x.rx_ctrl.noise_floor / 4.0,
            csi_info->x.rx_ctrl.ampdu_cnt,
            csi_info->x.rx_ctrl.channel,
            csi_info->x.rx_ctrl.secondary_channel,
            csi_info->x.rx_ctrl.timestamp / 1e6,
            csi_info->x.rx_ctrl.ant,
            csi_info->x.rx_ctrl.sig_len,
            csi_info->x.rx_ctrl.rx_state);

    const enum {NONE, ABOVE, BELOW} sec_chan = csi_info->x.rx_ctrl.secondary_channel;
    const bool is_ht   = csi_info->x.rx_ctrl.sig_mode;
    const int chan_bw  = csi_info->x.rx_ctrl.cwb == 0 ? 20 : 40;
    const bool is_stbc = csi_info->x.rx_ctrl.stbc;

    int8_t buff_csi[300];

    if(csi_info->buf[0] != 0) {
        ESP_LOGI(TAG, "json - non-zero guardband at t=%d", csi_info->x.rx_ctrl.timestamp);
    }

    // raw CSI

    // convert CSI buffer to base64 string and store in JSON buffer
    buff_printf(",\"csi_raw\": \"");

    size_t len_b64;
    retval = mbedtls_base64_encode((unsigned char *)(buff_json + len_json), len_buff_json - len_json, &len_b64, (const unsigned char *)csi_info->buf, csi_info->len);
    if(retval != 0) {
        ESP_LOGE(TAG, "base64 error");
        return retval;
    }
    len_json += len_b64;

    buff_printf("\"");


    buff_printf("}\n");

    return len_json;
}

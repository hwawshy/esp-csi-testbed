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

    int8_t buff_csi[128]; // only LLTF

    size_t len_csi = 0;
    int csi_first_ind = 0;

    // shift sub-carriers depending on secondary channel
    if(sec_chan == NONE) { // none
        memcpy(buff_csi + len_csi, csi_info->buf + 64, 64); len_csi += 64;
        memcpy(buff_csi + len_csi, csi_info->buf + 0, 64);  len_csi += 64;
        csi_first_ind = -32;
    } else if(sec_chan == BELOW) { // below
        memcpy(buff_csi + len_csi, csi_info->buf + 0, 128); len_csi += 128;
        csi_first_ind = 0;
    } else if(sec_chan == ABOVE) { // above
        memcpy(buff_csi + len_csi, csi_info->buf + 0, 128); len_csi += 128;
        csi_first_ind = -64;
    }

    size_t offset_csi = 0;
    if(csi_info->x.first_word_invalid) {
        ESP_LOGI(TAG, "invalid CSI data, skipping first 4 bytes");
        offset_csi    += 4;
        len_csi       -= 4;
        csi_first_ind += 2;
    }

    // convert CSI buffer to base64 string and store in JSON buffer
    buff_printf(",\"csi_raw\": \"");

    size_t len_b64;
    retval = mbedtls_base64_encode((unsigned char *)(buff_json + len_json), len_buff_json - len_json, &len_b64, (const unsigned char *)(buff_csi + offset_csi), len_csi);
    if(retval != 0) {
        ESP_LOGE(TAG, "base64 error");
        return retval;
    }
    len_json += len_b64;

    buff_printf("\",\"sc_ind_start\": %d,", csi_first_ind);
    buff_printf("\"csi_len\": %d", len_csi);

    buff_printf("}\n");

    return len_json;
}

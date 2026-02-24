#include "battery_cc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "battery_cc";
static const char *NVS_NAMESPACE = "battery_soh";
static const char *NVS_KEY_SOH = "soh";


esp_err_t bsoc_init(battery_soc_t *h, float capacity_Ah)
{
    if (!h || capacity_Ah <= 0 ) return ESP_ERR_INVALID_ARG;
    memset(h,0,sizeof(*h));
    h->last_sample_time_us = -1;
    h->last_I = 0.0f;
    h->cap_xa = 0.0;
    h->rated_capacity_Ah = capacity_Ah;
    h->eta_charge = 0.99070f;
    h->eta_discharge = 1.0f;
    h->soc_cc = 0.0f; // assume full unless loaded
    h->soh = 0.9749f;
    h->r_int = 0.05f; // default guess
    return ESP_OK;
}

void bsoc_deinit(battery_soc_t *h)
{
    if (!h) return;
    free(h->v_buf); free(h->i_buf);
    h->v_buf = NULL; h->i_buf = NULL;
}

// set efficiencies (0..1)
void bsoc_set_efficiencies(battery_soc_t *h, float eta_c, float eta_d)
{
    if (!h) return;
    h->eta_charge = fmaxf(0.5f, fminf(1.0f, eta_c));
    h->eta_discharge = fmaxf(0.5f, fminf(1.0f, eta_d));
}

void bsoc_set_rint(battery_soc_t *h, float r_ohm)
{
    if (!h) return;
    h->r_int = r_ohm;
}

// feed one sample (V in V, I in A). Convention: I>0 means discharge (current leaving battery)
void bsoc_feed_sample(battery_soc_t *h, float V_term, float I_A)
{
    if (!h) return;
    int64_t now_us = esp_timer_get_time(); // microseconds
    if (h->last_sample_time_us < 0) {
        // first sample, initialize buffers and timestamps
        h->last_sample_time_us = now_us;
        h->last_I = I_A;
        return;
    }

    double dt_s = (now_us - h->last_sample_time_us) * 1e-6; 
    
    // Trapezoid integration
    double delta_Q_Ah = 0.5 * (h->last_I + I_A) * dt_s / 3600.0; 
    
    
    // Convert to fraction of capacity (tính theo dung lượng thực tế = rated × soh)
    double actual_capacity = (double)h->rated_capacity_Ah * (double)h->soh;
    double delta_frac = (double)delta_Q_Ah / actual_capacity;
    
    
    if ((h->last_I + I_A)/2.0 < 0.0) {
        // discharging
        h->cap_xa += delta_Q_Ah;
        h->capacity_est += delta_Q_Ah;

        delta_frac *= h->eta_discharge;
    } else {
        h->cap_xa += delta_Q_Ah * h->eta_charge;
        h->capacity_est += delta_Q_Ah * h->eta_charge;
        // charging 
        delta_frac *= h->eta_charge; 
    }
    // update SOC (
    h->soc_cc += (float)delta_frac;
    if (h->soc_cc > 1.0f) h->soc_cc = 1.0f;
    if (h->soc_cc < 0.0f) h->soc_cc = 0.0f;

    h->last_sample_time_us = now_us;
    h->last_I = I_A;
}


float bsoc_get_soc_percent(battery_soc_t *h)
{
    if (!h) return 0.0f;
    return h->soc_cc * 100.0f;
}
float bsoc_get_soh_percent(battery_soc_t *h)
{
    if (!h) return 0.0f;
    return h->soh * 100.0f;
}


// Lưu giá trị SOH vào NVS
esp_err_t bsoh_store_to_nvs(battery_soc_t *h)
{
    if (!h) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi mở NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Lưu giá trị SOH (float)
    err = nvs_set_blob(nvs_handle, NVS_KEY_SOH, &h->soh, sizeof(float));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi lưu SOH vào NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi commit NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Đã lưu SOH=%.2f%% vào NVS", h->soh * 100.0f);
    }

    nvs_close(nvs_handle);
    return err;
}

// Lấy giá trị SOH từ NVS
esp_err_t bsoh_load_from_nvs(battery_soc_t *h)
{
    if (!h) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS chưa có dữ liệu, lưu SOH mặc định = %.2f%%", h->soh * 100.0f);
        // Lưu giá trị mặc định vào NVS
        return bsoh_store_to_nvs(h);
    }

    // Đọc giá trị SOH
    size_t required_size = sizeof(float);
    err = nvs_get_blob(nvs_handle, NVS_KEY_SOH, &h->soh, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Không tìm thấy SOH trong NVS, lưu giá trị mặc định = %.2f%%", h->soh * 100.0f);
        nvs_close(nvs_handle);
        // Lưu giá trị mặc định vào NVS
        return bsoh_store_to_nvs(h);
    }

    ESP_LOGI(TAG, "Đã đọc SOH=%.2f%% từ NVS", h->soh * 100.0f);
    nvs_close(nvs_handle);
    return ESP_OK;
}


// Hàm nội suy tuyến tính: OCV (V) -> SOC (%)
// SOC = a * OCV + b theo từng khoảng OCV
float ocv_to_soc(float ocv)
{
    float soc;
    
    // 1: 3.329 - 3.643 V | SOC = 15.9248 * OCV + (-53.0137)
    if (ocv < 3.643f) {
        soc = 15.9248f * ocv - 53.0137f;
    }
    // 2: 3.643 - 3.709 V | SOC = 38.1273 * OCV + (-133.8969)
    else if (ocv < 3.709f) {
        soc = 38.1273f * ocv - 133.8969f;
    }
    // 3: 3.709 - 3.740 V | SOC = 79.3466 * OCV + (-286.7603)
    else if (ocv < 3.740f) {
        soc = 79.3466f * ocv - 286.7603f;
    }
    // 4: 3.740 - 3.808 V | SOC = 147.2211 * OCV + (-540.6148)
    else if (ocv < 3.808f) {
        soc = 147.2211f * ocv - 540.6148f;
    }
    // 5: 3.808 - 3.854 V | SOC = 215.8250 * OCV + (-801.8569)
    else if (ocv < 3.854f) {
        soc = 215.8250f * ocv - 801.8569f;
    }
    // 6: 3.854 - 3.913 V | SOC = 257.5007 * OCV + (-962.4878)
    else if (ocv < 3.913f) {
        soc = 257.5007f * ocv - 962.4878f;
    }
    // 7: 3.913 - 3.972 V | SOC = 251.0478 * OCV + (-937.2406)
    else if (ocv < 3.972f) {
        soc = 251.0478f * ocv - 937.2406f;
    }
    // 8: 3.972 - 4.001 V | SOC = 349.6884 * OCV + (-1329.0718)
    else if (ocv < 4.001f) {
        soc = 349.6884f * ocv - 1329.0718f;
    }
    // 9: 4.001 - 4.030 V | SOC = 346.3950 * OCV + (-1315.8955)
    else if (ocv < 4.030f) {
        soc = 346.3950f * ocv - 1315.8955f;
    }
    // 10: 4.030 - 4.048 V | SOC = 538.8249 * OCV + (-2091.3454)
    else if (ocv < 4.048f) {
        soc = 538.8249f * ocv - 2091.3454f;
    }
    // 11: 4.048 - 4.080 V | SOC = 315.8362 * OCV + (-1188.6117)
    else {
        soc = 315.8362f * ocv - 1188.6117f;
    }
    
    // Clamp SOC to [0, 100]
    if (soc < 0.0f) soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;

    return soc;
}

// Ước lượng SOC ban đầu từ OCV
esp_err_t bsoc_estimate_soc_from_ocv(battery_soc_t *h, float ocv_voltage)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    
    float soc_percent = ocv_to_soc(ocv_voltage);
    h->soc_cc = soc_percent / 100.0f; // Chuyển % sang 0..1
    
    // Cập nhật capacity_est theo SOC mới
    h->capacity_est = h->soc_cc * h->rated_capacity_Ah * h->soh;
    
    ESP_LOGI(TAG, "Sơ khởi SOC từ OCV=%.3fV -> SOC=%.2f%%, capacity_est=%.6fAh", 
             ocv_voltage, soc_percent, h->capacity_est);
    return ESP_OK;
}

// Cập nhật SOH khi sạc đầy 
void bsoc_update_soh_on_full_charge(battery_soc_t *h)
{
    if (!h) return;
    
    // Chỉ cập nhật SOH nếu SOC >= 97% (tránh cập nhật sai khi bộ sạc tự ngắt sớm)
    if (h->soc_cc < 0.97f) {
        ESP_LOGW(TAG, "SOC chỉ %.1f%% < 96%%, không cập nhật SOH (bộ sạc có thể tự ngắt sớm)", h->soc_cc * 100.0f);
        return;
    }
    
    // Nếu SOC không đạt 100% khi sạc đầy -> SOH giảm
    if (h->soc_cc < 1.0f) {
        float old_soh = h->soh;
        h->soh = h->soc_cc * h->soh; // SOH_mới = SOC_hiện_tại * SOH_cũ
        
        if (h->soh < 0.5f) h->soh = 0.5f; 
        
        ESP_LOGI(TAG, "Cập nhật SOH khi sạc đầy: SOC=%.1f%% -> SOH: %.1f%% -> %.1f%%",
                 h->soc_cc * 100.0f, old_soh * 100.0f, h->soh * 100.0f);
        
        // Lưu SOH mới vào NVS
        bsoh_store_to_nvs(h);
    } else {
        ESP_LOGI(TAG, "Sạc đầy đạt 100%%, giữ SOH=%.1f%%", h->soh * 100.0f);
    }
}

// Cập nhật SOH khi xả hết (điện áp ≤ 2.6V)
void bsoc_update_soh_on_full_discharge(battery_soc_t *h)
{
    if (!h) return;
    
    // Nếu SOC không về 0% khi xả hết -> SOH giảm
    if (h->soc_cc > 0.0f) {
        float old_soh = h->soh;
        // SOH_mới = (1 - SOC_còn_lại) * SOH_cũ
        h->soh = (1.0f - h->soc_cc) * h->soh;
        
        if (h->soh < 0.5f) h->soh = 0.5f; // Giới hạn tối thiểu 50%
        
        ESP_LOGI(TAG, "Cập nhật SOH khi xả hết: SOC=%.1f%% -> SOH: %.1f%% -> %.1f%%",
                 h->soc_cc * 100.0f, old_soh * 100.0f, h->soh * 100.0f);
        
        // Lưu SOH mới vào NVS
        bsoh_store_to_nvs(h);
    } else {
        ESP_LOGI(TAG, "Xả hết đạt 0%%, giữ SOH=%.1f%%", h->soh * 100.0f);
    }
}

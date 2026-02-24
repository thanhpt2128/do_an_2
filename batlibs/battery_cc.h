#ifndef BATTERY_CC_H
#define BATTERY_CC_H

#pragma once
#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // params
    double cap_xa;                   // dung luong da xa/sac
    double capacity_est;          // dung luong uoc luong
    float rated_capacity_Ah;      // dung lượng danh định (Ah)
    int64_t last_sample_time_us;  // thời gian lấy mẫu trước đó
    float last_I;

    int window_size;        // kích thước buffer cho universal
    float eta_charge;       // hiệu suất sạc (0..1)
    float eta_discharge;    // hiệu suất xả (0..1)
    // runtime
    float soc_cc;           // coulomb counting SOC [0..1]
    float soh;              // soh của pin (0..1), mặc định 1.0
    // internal buffers (allocated theo window_size)
    float *v_buf;
    float *i_buf;
    int buf_idx;
    int buf_count;
    // simple IR (fallback)
    float r_int;            // internal resistance estimate (ohm)
} battery_soc_t;

esp_err_t bsoc_init(battery_soc_t *h, float capacity_Ah);
void bsoc_deinit(battery_soc_t *h);

void bsoc_set_efficiencies(battery_soc_t *h, float eta_c, float eta_d);
void bsoc_set_rint(battery_soc_t *h, float r_ohm);

void bsoc_feed_sample(battery_soc_t *h, float V_term, float I_A);
float bsoc_get_soc_percent(battery_soc_t *h);
float bsoc_get_soh_percent(battery_soc_t *h);

// Ước lượng SOC ban đầu từ OCV
esp_err_t bsoc_estimate_soc_from_ocv(battery_soc_t *h, float ocv_voltage);

// Cập nhật SOH khi sạc đầy hoặc xả hết
void bsoc_update_soh_on_full_charge(battery_soc_t *h);
void bsoc_update_soh_on_full_discharge(battery_soc_t *h);

// Run universal deconvolution on current buffer; if success updates soc_cc
esp_err_t bsoc_run_universal(battery_soc_t *h, float reg_lambda);

// optional persistence (NVS) - simple prototypes
esp_err_t bsoh_store_to_nvs(battery_soc_t *h);
esp_err_t bsoh_load_from_nvs(battery_soc_t *h);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_CC_H

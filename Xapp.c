/*
 * Dynamic Traffic-Aware xApp with Relative PRB Allocation
 * Version: 3.0 - Final Fixed
 */

#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>

// FlexRIC Headers (تنظیم بر اساس مسیر نصب شما)
#include "xapp/e42_xapp_api.h"
#include "xapp/near_ric_api.h"
#include "sm/rc_sm/rc_sm_id.h"
#include "sm/kpm_sm/kpm_sm_id.h"

// ==================== Configuration ====================
#define TOTAL_PRB_AVAILABLE 273  // تعداد کل PRB موجود در سیستم
#define NORMAL_PRB_PERCENTAGE 30.0  // 30% برای وضعیت عادی
#define BURST_PRB_PERCENTAGE 70.0   // 70% برای وضعیت انفجاری

#define THRESHOLD_ENTRY_KBPS 15000  // آستانه ورود به حالت Burst
#define THRESHOLD_EXIT_KBPS  12000  // آستانه خروج از Burst (Hysteresis)

#define KPM_REPORT_PERIOD_MS 1000
#define RC_CONTROL_CHECK_INTERVAL_MS 500

// RAN Parameters (تنظیم بر اساس RAN شما)
#define RAN_PARAM_SLICE_ID 1
#define RAN_PARAM_PRB_ALLOC_ID 100  // فرضی - باید از مستندات RAN تأیید شود

// ==================== Global State ====================
typedef enum {
    TRAFFIC_NORMAL,
    TRAFFIC_BURST
} traffic_state_t;

static pthread_mutex_t kpm_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t rc_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rc_cond = PTHREAD_COND_INITIALIZER;

static traffic_state_t current_state = TRAFFIC_NORMAL;
static float latest_throughput_kbps = 0.0;
static int latest_prb_allocated = 0;
static int state_change_requested = 0;

// ==================== Helper Functions ====================
static inline int calculate_prb_allocation(traffic_state_t state) {
    float percentage = (state == TRAFFIC_BURST) ? 
                       BURST_PRB_PERCENTAGE : NORMAL_PRB_PERCENTAGE;
    return (int)((percentage / 100.0) * TOTAL_PRB_AVAILABLE);
}

static void log_state_change(traffic_state_t old_state, traffic_state_t new_state,
                             float throughput, int old_prb, int new_prb) {
    printf("\n========== STATE TRANSITION ==========\n");
    printf("Time: %ld\n", time(NULL));
    printf("Throughput: %.2f kbps\n", throughput);
    printf("State: %s -> %s\n",
           old_state == TRAFFIC_NORMAL ? "NORMAL" : "BURST",
           new_state == TRAFFIC_NORMAL ? "NORMAL" : "BURST");
    printf("PRB Allocation: %d -> %d (%.1f%% -> %.1f%%)\n",
           old_prb, new_prb,
           (old_prb * 100.0) / TOTAL_PRB_AVAILABLE,
           (new_prb * 100.0) / TOTAL_PRB_AVAILABLE);
    printf("======================================\n\n");
}

// ==================== KPM Callback ====================
static void sm_cb_kpm(sm_ag_if_rd_t const *rd) {
    assert(rd != NULL);
    assert(rd->type == INDICATION_MSG_AGENT_IF_ANS_V0);
    assert(rd->ind.type == KPM_STATS_V3_0);

    // استخراج داده‌های KPM (باید بر اساس ساختار دقیق SDK تنظیم شود)
    kpm_ind_data_t const *ind = &rd->ind.kpm.ind;
    
    // فرض: استخراج throughput از اولین measurement
    // این بخش باید بر اساس ساختار دقیق KPM Indication شما تنظیم شود
    float throughput_kbps = 0.0;
    
    // مثال ساده (باید با ساختار واقعی جایگزین شود):
    if (ind->hdr.format == KPM_V3_0 && ind->msg.frm_3.meas_data_lst_len > 0) {
        // فرض: اولین measurement throughput است
        kpm_meas_data_t const *meas = &ind->msg.frm_3.meas_data_lst[0];
        if (meas->meas_record_len > 0) {
            // فرض: مقدار integer است و باید به kbps تبدیل شود
            throughput_kbps = (float)meas->meas_record_lst[0].int_val;
        }
    }

    printf("[KPM] Received Throughput: %.2f kbps\n", throughput_kbps);

    // بروزرسانی وضعیت با Hysteresis
    pthread_mutex_lock(&kpm_mutex);
    latest_throughput_kbps = throughput_kbps;
    
    traffic_state_t old_state = current_state;
    traffic_state_t new_state = old_state;
    
    // منطق Hysteresis
    if (current_state == TRAFFIC_NORMAL && throughput_kbps > THRESHOLD_ENTRY_KBPS) {
        new_state = TRAFFIC_BURST;
    } else if (current_state == TRAFFIC_BURST && throughput_kbps < THRESHOLD_EXIT_KBPS) {
        new_state = TRAFFIC_NORMAL;
    }
    
    if (new_state != old_state) {
        int old_prb = calculate_prb_allocation(old_state);
        int new_prb = calculate_prb_allocation(new_state);
        
        current_state = new_state;
        latest_prb_allocated = new_prb;
        state_change_requested = 1;
        
        log_state_change(old_state, new_state, throughput_kbps, old_prb, new_prb);
        
        pthread_cond_signal(&rc_cond);
    }
    pthread_mutex_unlock(&kpm_mutex);
}

// ==================== RC Control Thread ====================
static void *rc_control_thread(void *arg) {
    e42_xapp_t *xapp = (e42_xapp_t *)arg;
    
    while (1) {
        pthread_mutex_lock(&rc_mutex);
        
        // منتظر تغییر وضعیت یا timeout
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += RC_CONTROL_CHECK_INTERVAL_MS / 1000;
        ts.tv_nsec += (RC_CONTROL_CHECK_INTERVAL_MS % 1000) * 1000000;
        
        pthread_cond_timedwait(&rc_cond, &rc_mutex, &ts);
        
        if (state_change_requested) {
            state_change_requested = 0;
            
            // آماده‌سازی RC Control Message
            rc_ctrl_req_data_t rc_ctrl = {0};
            rc_ctrl.hdr.format = FORMAT_1_E2SM_RC_CTRL_HDR;
            rc_ctrl.hdr.frmt_1.ric_style_type = 3; // باید از مستندات RAN تأیید شود
            rc_ctrl.hdr.frmt_1.ctrl_act_id = 6;    // مثال
            
            rc_ctrl.msg.format = FORMAT_1_E2SM_RC_CTRL_MSG;
            
            // تنظیم PRB Allocation
            rc_ctrl.msg.frmt_1.sz_ran_param = 1;
            rc_ctrl.msg.frmt_1.ran_param = calloc(1, sizeof(seq_ran_param_3_t));
            assert(rc_ctrl.msg.frmt_1.ran_param != NULL);
            
            rc_ctrl.msg.frmt_1.ran_param[0].ran_param_id = RAN_PARAM_PRB_ALLOC_ID;
            rc_ctrl.msg.frmt_1.ran_param[0].ran_param_val.type = ELEMENT_KEY_FLAG_FALSE;
            
            int prb_to_allocate = latest_prb_allocated;
            rc_ctrl.msg.frmt_1.ran_param[0].ran_param_val.flag_false = 
                calloc(1, sizeof(ran_parameter_value_t));
            rc_ctrl.msg.frmt_1.ran_param[0].ran_param_val.flag_false->type = 
                INTEGER_RAN_PARAMETER_VALUE;
            rc_ctrl.msg.frmt_1.ran_param[0].ran_param_val.flag_false->int_ran = 
                prb_to_allocate;
            
            printf("[RC] Sending Control: PRB=%d (%.1f%%)\n",
                   prb_to_allocate,
                   (prb_to_allocate * 100.0) / TOTAL_PRB_AVAILABLE);
            
            control_sm_xapp_api(xapp, RAN_PARAM_SLICE_ID, &rc_ctrl);
            
            // آزادسازی حافظه
            free(rc_ctrl.msg.frmt_1.ran_param[0].ran_param_val.flag_false);
            free(rc_ctrl.msg.frmt_1.ran_param);
        }
        
        pthread_mutex_unlock(&rc_mutex);
    }
    
    return NULL;
}

// ==================== Main ====================
int main(int argc, char *argv[]) {
    // مقداردهی اولیه
    latest_prb_allocated = calculate_prb_allocation(TRAFFIC_NORMAL);
    
    printf("=== Dynamic Traffic-Aware xApp ===\n");
    printf("Total PRB Available: %d\n", TOTAL_PRB_AVAILABLE);
    printf("Normal State: %d PRB (%.1f%%)\n",
           (int)(NORMAL_PRB_PERCENTAGE * TOTAL_PRB_AVAILABLE / 100.0),
           NORMAL_PRB_PERCENTAGE);
    printf("Burst State: %d PRB (%.1f%%)\n",
           (int)(BURST_PRB_PERCENTAGE * TOTAL_PRB_AVAILABLE / 100.0),
           BURST_PRB_PERCENTAGE);
    printf("Entry Threshold: %d kbps\n", THRESHOLD_ENTRY_KBPS);
    printf("Exit Threshold: %d kbps (Hysteresis)\n", THRESHOLD_EXIT_KBPS);
    printf("==================================\n\n");
    
    // اتصال به Near-RT RIC
    fr_args_t args = init_fr_args(argc, argv);
    e42_xapp_t *xapp = init_xapp_api(&args);
    assert(xapp != NULL);
    
    // Subscribe به KPM
    sm_ans_xapp_t kpm_sub = {0};
    kpm_sub.type = KPM_V3_0_SM_ID;
    kpm_sub.kpm.sub.type = SUBSCRIPTION_FLRC;
    kpm_sub.kpm.sub.flrc.ric_req_id = 1;
    kpm_sub.kpm.sub.flrc.report_period_ms = KPM_REPORT_PERIOD_MS;
    
    report_service_xapp_api(xapp, RAN_PARAM_SLICE_ID, &kpm_sub, sm_cb_kpm);
    
    // راه‌اندازی RC Control Thread
    pthread_t rc_thread;
    pthread_create(&rc_thread, NULL, rc_control_thread, xapp);
    
    // حلقه اصلی
    sleep(100000);
    
    // پاکسازی
    pthread_cancel(rc_thread);
    pthread_join(rc_thread, NULL);
    
    rm_report_service_xapp_api(xapp, kpm_sub.kpm.sub.flrc.ric_req_id);
    
    while (try_stop_xapp_api(xapp) == false)
        usleep(1000);
    
    pthread_mutex_destroy(&kpm_mutex);
    pthread_mutex_destroy(&rc_mutex);
    pthread_cond_destroy(&rc_cond);
    
    printf("xApp terminated successfully\n");
    return 0;
}

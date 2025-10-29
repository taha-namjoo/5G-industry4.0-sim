

#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/util/alg_ds/alg/defer.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/util/e.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>

static uint64_t const period_ms = 1000;
static pthread_mutex_t mtx;
static FILE* csv_file = NULL;

typedef struct {
    uint64_t ue_ngap_id;
    uint64_t ran_ue_id;
    int prb_tot_dl;
    int prb_tot_ul;
    int pdcp_volume_dl;
    int pdcp_volume_ul;
    float rlc_delay_dl;
    float ue_thp_dl;
    float ue_thp_ul;
} ue_measurement_t;

static ue_measurement_t ue_measurements[2] = {0};
static size_t num_ues = 0;

static void init_csv_file(void) {
    csv_file = fopen("/home/tahanamjoo/kpm_monitoring.csv", "w");
    if (csv_file == NULL) {
        perror("Failed to open CSV file");
        return;
    }
    
    fprintf(csv_file, "timestamp,indication_counter,latency_us,");
    fprintf(csv_file, "ue1_ngap_id,ue1_ran_ue_id,ue1_prb_dl,ue1_prb_ul,ue1_pdcp_dl_kb,ue1_pdcp_ul_kb,ue1_delay_us,ue1_thp_dl_kbps,ue1_thp_ul_kbps,");
    fprintf(csv_file, "ue2_ngap_id,ue2_ran_ue_id,ue2_prb_dl,ue2_prb_ul,ue2_pdcp_dl_kb,ue2_pdcp_ul_kb,ue2_delay_us,ue2_thp_dl_kbps,ue2_thp_ul_kbps\n");
    fflush(csv_file);
    
    printf("[CSV]: Log file created at /home/tahanamjoo/kpm_monitoring.csv\n");
}

static void close_csv_file(void) {
    if (csv_file != NULL) {
        fflush(csv_file);
        fclose(csv_file);
        csv_file = NULL;
    }
}

static void log_to_csv(int64_t timestamp, int counter, int64_t latency) {
    if (csv_file == NULL) return;
    
    fprintf(csv_file, "%ld,%d,%ld", timestamp, counter, latency);
    
    if (num_ues >= 1) {
        fprintf(csv_file, ",%lu,%lu,%d,%d,%d,%d,%.2f,%.2f,%.2f",
                ue_measurements[0].ue_ngap_id,
                ue_measurements[0].ran_ue_id,
                ue_measurements[0].prb_tot_dl,
                ue_measurements[0].prb_tot_ul,
                ue_measurements[0].pdcp_volume_dl,
                ue_measurements[0].pdcp_volume_ul,
                ue_measurements[0].rlc_delay_dl,
                ue_measurements[0].ue_thp_dl,
                ue_measurements[0].ue_thp_ul);
    } else {
        fprintf(csv_file, ",0,0,0,0,0,0,0.0,0.0,0.0");
    }
    
    if (num_ues >= 2) {
        fprintf(csv_file, ",%lu,%lu,%d,%d,%d,%d,%.2f,%.2f,%.2f",
                ue_measurements[1].ue_ngap_id,
                ue_measurements[1].ran_ue_id,
                ue_measurements[1].prb_tot_dl,
                ue_measurements[1].prb_tot_ul,
                ue_measurements[1].pdcp_volume_dl,
                ue_measurements[1].pdcp_volume_ul,
                ue_measurements[1].rlc_delay_dl,
                ue_measurements[1].ue_thp_dl,
                ue_measurements[1].ue_thp_ul);
    } else {
        fprintf(csv_file, ",0,0,0,0,0,0,0.0,0.0,0.0");
    }
    
    fprintf(csv_file, "\n");
    fflush(csv_file);
}

static void log_gnb_ue_id(ue_id_e2sm_t ue_id) {
    if (ue_id.gnb.gnb_cu_ue_f1ap_lst != NULL) {
        for (size_t i = 0; i < ue_id.gnb.gnb_cu_ue_f1ap_lst_len; i++) {
            printf("UE ID type = gNB-CU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb.gnb_cu_ue_f1ap_lst[i]);
        }
    } else {
        printf("UE ID type = gNB, amf_ue_ngap_id = %lu\n", ue_id.gnb.amf_ue_ngap_id);
    }
    if (ue_id.gnb.ran_ue_id != NULL) {
        printf("ran_ue_id = %lx\n", *ue_id.gnb.ran_ue_id);
    }
}

static void log_du_ue_id(ue_id_e2sm_t ue_id) {
    printf("UE ID type = gNB-DU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb_du.gnb_cu_ue_f1ap);
    if (ue_id.gnb_du.ran_ue_id != NULL) {
        printf("ran_ue_id = %lx\n", *ue_id.gnb_du.ran_ue_id);
    }
}

static void log_cuup_ue_id(ue_id_e2sm_t ue_id) {
    printf("UE ID type = gNB-CU-UP, gnb_cu_cp_ue_e1ap = %u\n", ue_id.gnb_cu_up.gnb_cu_cp_ue_e1ap);
    if (ue_id.gnb_cu_up.ran_ue_id != NULL) {
        printf("ran_ue_id = %lx\n", *ue_id.gnb_cu_up.ran_ue_id);
    }
}

typedef void (*log_ue_id)(ue_id_e2sm_t ue_id);

static log_ue_id log_ue_id_e2sm[END_UE_ID_E2SM] = {
    log_gnb_ue_id,
    log_du_ue_id,
    log_cuup_ue_id,
    NULL,
    NULL,
    NULL,
    NULL,
};

static void log_int_value(byte_array_t name, meas_record_lst_t meas_record, ue_measurement_t* meas) {
    if (cmp_str_ba("RRU.PrbTotDl", name) == 0) {
        printf("RRU.PrbTotDl = %d [PRBs]\n", meas_record.int_val);
        if (meas) meas->prb_tot_dl = meas_record.int_val;
    } else if (cmp_str_ba("RRU.PrbTotUl", name) == 0) {
        printf("RRU.PrbTotUl = %d [PRBs]\n", meas_record.int_val);
        if (meas) meas->prb_tot_ul = meas_record.int_val;
    } else if (cmp_str_ba("DRB.PdcpSduVolumeDL", name) == 0) {
        printf("DRB.PdcpSduVolumeDL = %d [kb]\n", meas_record.int_val);
        if (meas) meas->pdcp_volume_dl = meas_record.int_val;
    } else if (cmp_str_ba("DRB.PdcpSduVolumeUL", name) == 0) {
        printf("DRB.PdcpSduVolumeUL = %d [kb]\n", meas_record.int_val);
        if (meas) meas->pdcp_volume_ul = meas_record.int_val;
    } else {
        printf("Measurement Name not yet supported\n");
    }
}

static void log_real_value(byte_array_t name, meas_record_lst_t meas_record, ue_measurement_t* meas) {
    if (cmp_str_ba("DRB.RlcSduDelayDl", name) == 0) {
        printf("DRB.RlcSduDelayDl = %.2f [μs]\n", meas_record.real_val);
        if (meas) meas->rlc_delay_dl = meas_record.real_val;
    } else if (cmp_str_ba("DRB.UEThpDl", name) == 0) {
        printf("DRB.UEThpDl = %.2f [kbps]\n", meas_record.real_val);
        if (meas) meas->ue_thp_dl = meas_record.real_val;
    } else if (cmp_str_ba("DRB.UEThpUl", name) == 0) {
        printf("DRB.UEThpUl = %.2f [kbps]\n", meas_record.real_val);
        if (meas) meas->ue_thp_ul = meas_record.real_val;
    } else {
        printf("Measurement Name not yet supported\n");
    }
}

typedef void (*log_meas_value)(byte_array_t name, meas_record_lst_t meas_record, ue_measurement_t* meas);

static log_meas_value get_meas_value[END_MEAS_VALUE] = {
    log_int_value,
    log_real_value,
    NULL,
};

static void match_meas_name_type(meas_type_t meas_type, meas_record_lst_t meas_record, ue_measurement_t* meas) {
    get_meas_value[meas_record.value](meas_type.name, meas_record, meas);
}

static void match_id_meas_type(meas_type_t meas_type, meas_record_lst_t meas_record, ue_measurement_t* meas) {
    (void)meas_type;
    (void)meas_record;
    (void)meas;
    assert(false && "ID Measurement Type not yet supported");
}

typedef void (*check_meas_type)(meas_type_t meas_type, meas_record_lst_t meas_record, ue_measurement_t* meas);

static check_meas_type match_meas_type[END_MEAS_TYPE] = {
    match_meas_name_type,
    match_id_meas_type,
};

static void log_kpm_measurements(kpm_ind_msg_format_1_t const* msg_frm_1, ue_measurement_t* meas) {
    assert(msg_frm_1->meas_info_lst_len > 0 && "Cannot correctly print measurements");

    for (size_t j = 0; j < msg_frm_1->meas_data_lst_len; j++) {
        meas_data_lst_t const data_item = msg_frm_1->meas_data_lst[j];
        for (size_t z = 0; z < data_item.meas_record_len; z++) {
            meas_type_t const meas_type = msg_frm_1->meas_info_lst[z].meas_type;
            meas_record_lst_t const record_item = data_item.meas_record_lst[z];
            match_meas_type[meas_type.type](meas_type, record_item, meas);
            if (data_item.incomplete_flag && *data_item.incomplete_flag == TRUE_ENUM_VALUE)
                printf("Measurement Record not reliable\n");
        }
    }
}

static void sm_cb_kpm(sm_ag_if_rd_t const* rd) {
    assert(rd != NULL);
    assert(rd->type == INDICATION_MSG_AGENT_IF_ANS_V0);
    assert(rd->ind.type == KPM_STATS_V3_0);

    kpm_ind_data_t const* ind = &rd->ind.kpm.ind;
    kpm_ric_ind_hdr_format_1_t const* hdr_frm_1 = &ind->hdr.kpm_ric_ind_hdr_format_1;
    kpm_ind_msg_format_3_t const* msg_frm_3 = &ind->msg.frm_3;

    int64_t const now = time_now_us();
    static int counter = 1;
    {
        lock_guard(&mtx);

        int64_t latency = now - hdr_frm_1->collectStartTime;
        printf("\n%7d KPM ind_msg latency = %ld [μs]\n", counter, latency);

        memset(ue_measurements, 0, sizeof(ue_measurements));
        num_ues = msg_frm_3->ue_meas_report_lst_len;
        if (num_ues > 2) num_ues = 2;

        for (size_t i = 0; i < num_ues && i < 2; i++) {
            ue_id_e2sm_t const ue_id_e2sm = msg_frm_3->meas_report_per_ue[i].ue_meas_report_lst;
            ue_id_e2sm_e const type = ue_id_e2sm.type;
            
            ue_measurements[i].ue_ngap_id = ue_id_e2sm.gnb.amf_ue_ngap_id;
            if (ue_id_e2sm.gnb.ran_ue_id != NULL) {
                ue_measurements[i].ran_ue_id = *ue_id_e2sm.gnb.ran_ue_id;
            }
            
            log_ue_id_e2sm[type](ue_id_e2sm);

            log_kpm_measurements(&msg_frm_3->meas_report_per_ue[i].ind_msg_format_1, &ue_measurements[i]);
        }
        
        log_to_csv(now, counter, latency);
        counter++;
    }
}

static test_info_lst_t filter_predicate(test_cond_type_e type, test_cond_e cond, int value) {
    test_info_lst_t dst = {0};
    dst.test_cond_type = type;
    dst.S_NSSAI = TRUE_TEST_COND_TYPE;
    dst.test_cond = calloc(1, sizeof(test_cond_e));
    assert(dst.test_cond != NULL && "Memory exhausted");
    *dst.test_cond = cond;
    dst.test_cond_value = calloc(1, sizeof(test_cond_value_t));
    assert(dst.test_cond_value != NULL && "Memory exhausted");
    dst.test_cond_value->type = OCTET_STRING_TEST_COND_VALUE;
    dst.test_cond_value->octet_string_value = calloc(1, sizeof(byte_array_t));
    assert(dst.test_cond_value->octet_string_value != NULL && "Memory exhausted");
    const size_t len_nssai = 1;
    dst.test_cond_value->octet_string_value->len = len_nssai;
    dst.test_cond_value->octet_string_value->buf = calloc(len_nssai, sizeof(uint8_t));
    assert(dst.test_cond_value->octet_string_value->buf != NULL && "Memory exhausted");
    dst.test_cond_value->octet_string_value->buf[0] = value;
    return dst;
}

static label_info_lst_t fill_kpm_label(void) {
    label_info_lst_t label_item = {0};
    label_item.noLabel = ecalloc(1, sizeof(enum_value_e));
    *label_item.noLabel = TRUE_ENUM_VALUE;
    return label_item;
}

static kpm_act_def_format_1_t fill_act_def_frm_1(ric_report_style_item_t const* report_item) {
    assert(report_item != NULL);
    kpm_act_def_format_1_t ad_frm_1 = {0};
    size_t const sz = report_item->meas_info_for_action_lst_len;
    ad_frm_1.meas_info_lst_len = sz;
    ad_frm_1.meas_info_lst = calloc(sz, sizeof(meas_info_format_1_lst_t));
    assert(ad_frm_1.meas_info_lst != NULL && "Memory exhausted");
    for (size_t i = 0; i < sz; i++) {
        meas_info_format_1_lst_t* meas_item = &ad_frm_1.meas_info_lst[i];
        meas_item->meas_type.type = NAME_MEAS_TYPE;
        meas_item->meas_type.name = copy_byte_array(report_item->meas_info_for_action_lst[i].name);
        meas_item->label_info_lst_len = 1;
        meas_item->label_info_lst = ecalloc(1, sizeof(label_info_lst_t));
        meas_item->label_info_lst[0] = fill_kpm_label();
    }
    ad_frm_1.gran_period_ms = period_ms;
    ad_frm_1.cell_global_id = NULL;
#if defined KPM_V2_03 || defined KPM_V3_00
    ad_frm_1.meas_bin_range_info_lst_len = 0;
    ad_frm_1.meas_bin_info_lst = NULL;
#endif
    return ad_frm_1;
}

static kpm_act_def_t fill_report_style_4(ric_report_style_item_t const* report_item) {
    assert(report_item != NULL);
    assert(report_item->act_def_format_type == FORMAT_4_ACTION_DEFINITION);
    kpm_act_def_t act_def = {.type = FORMAT_4_ACTION_DEFINITION};
    act_def.frm_4.matching_cond_lst_len = 1;
    act_def.frm_4.matching_cond_lst = calloc(act_def.frm_4.matching_cond_lst_len, sizeof(matching_condition_format_4_lst_t));
    assert(act_def.frm_4.matching_cond_lst != NULL && "Memory exhausted");
    test_cond_type_e const type = S_NSSAI_TEST_COND_TYPE;
    test_cond_e const condition = EQUAL_TEST_COND;
    int const value = 1;
    act_def.frm_4.matching_cond_lst[0].test_info_lst = filter_predicate(type, condition, value);
    act_def.frm_4.action_def_format_1 = fill_act_def_frm_1(report_item);
    return act_def;
}

typedef kpm_act_def_t (*fill_kpm_act_def)(ric_report_style_item_t const* report_item);

static fill_kpm_act_def get_kpm_act_def[END_RIC_SERVICE_REPORT] = {
    NULL,
    NULL,
    NULL,
    fill_report_style_4,
    NULL,
};

static kpm_sub_data_t gen_kpm_subs(kpm_ran_function_def_t const* ran_func) {
    assert(ran_func != NULL);
    assert(ran_func->ric_event_trigger_style_list != NULL);
    kpm_sub_data_t kpm_sub = {0};
    assert(ran_func->ric_event_trigger_style_list[0].format_type == FORMAT_1_RIC_EVENT_TRIGGER);
    kpm_sub.ev_trg_def.type = FORMAT_1_RIC_EVENT_TRIGGER;
    kpm_sub.ev_trg_def.kpm_ric_event_trigger_format_1.report_period_ms = period_ms;
    kpm_sub.sz_ad = 1;
    kpm_sub.ad = calloc(kpm_sub.sz_ad, sizeof(kpm_act_def_t));
    assert(kpm_sub.ad != NULL && "Memory exhausted");
    ric_report_style_item_t* const report_item = &ran_func->ric_report_style_list[0];
    ric_service_report_e const report_style_type = report_item->report_style_type;
    *kpm_sub.ad = get_kpm_act_def[report_style_type](report_item);
    return kpm_sub;
}

static bool eq_sm(sm_ran_function_t const* elem, int const id) {
    return (elem->id == id);
}

static size_t find_sm_idx(sm_ran_function_t* rf, size_t sz, bool (*f)(sm_ran_function_t const*, int const), int const id) {
    for (size_t i = 0; i < sz; i++) {
        if (f(&rf[i], id))
            return i;
    }
    assert(0 != 0 && "SM ID could not be found in the RAN Function List");
}

int main(int argc, char* argv[]) {
    fr_args_t args = init_fr_args(argc, argv);

    init_xapp_api(&args);
    sleep(1);

    e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
    defer({ free_e2_node_arr_xapp(&nodes); });

    assert(nodes.len > 0);

    printf("Connected E2 nodes = %d\n", nodes.len);

    pthread_mutexattr_t attr = {0};
    int rc = pthread_mutex_init(&mtx, &attr);
    assert(rc == 0);

    init_csv_file();

    sm_ans_xapp_t* hndl = calloc(nodes.len, sizeof(sm_ans_xapp_t));
    assert(hndl != NULL);

    int const KPM_ran_function = 2;

    for (size_t i = 0; i < nodes.len; ++i) {
        e2_node_connected_xapp_t* n = &nodes.n[i];
        size_t const idx = find_sm_idx(n->rf, n->len_rf, eq_sm, KPM_ran_function);
        assert(n->rf[idx].defn.type == KPM_RAN_FUNC_DEF_E && "KPM is not the received RAN Function");
        if (n->rf[idx].defn.kpm.ric_report_style_list != NULL) {
            kpm_sub_data_t kpm_sub = gen_kpm_subs(&n->rf[idx].defn.kpm);
            hndl[i] = report_sm_xapp_api(&n->id, KPM_ran_function, &kpm_sub, sm_cb_kpm);
            assert(hndl[i].success == true);
            free_kpm_sub_data(&kpm_sub);
        }
    }

    printf("[MAIN]: KPM monitoring started with CSV logging\n");
    	int running = 1;
while (running) {
    sleep(1);  
}
    
    xapp_wait_end_api();

    for (int i = 0; i < nodes.len; ++i) {
        if (hndl[i].success == true)
            rm_report_sm_xapp_api(hndl[i].u.handle);
    }
    free(hndl);

    close_csv_file();

    while (try_stop_xapp_api() == false)
        usleep(1000);

    rc = pthread_mutex_destroy(&mtx);
    assert(rc == 0);

    printf("[KPM]: Test xApp run SUCCESSFULLY\n");
    
    return 0;
}
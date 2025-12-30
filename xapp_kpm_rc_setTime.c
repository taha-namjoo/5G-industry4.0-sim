
#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_struct.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_list.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/util/e.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>

static ue_id_e2sm_t ue_id;
static uint64_t const period_ms = 100;
static pthread_mutex_t mtx;
static FILE* csv_file = NULL;

// Configuration thresholds
#define TOTAL_PRB_POOL 106  // Total PRBs based on network logs
#define BURST_DETECTION_THRESHOLD 15000.0  // kbps
#define NORMAL_PRB_ALLOCATION 50
#define BURST_PRB_ALLOCATION 76  // Adjusted to ensure total <= 106
#define MIN_PRB_ALLOCATION 30

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
    int is_burst;
} ue_measurement_t;

static ue_measurement_t ue_measurements[2] = {0};
static size_t num_ues = 0;

typedef struct {
    int drb_id;
    int qfi;
    int mapping_ind;
} rc_allocation_t;

static rc_allocation_t rc_alloc = {0};

typedef struct {
    int ue_index;
    int drb_id;
    int qfi;
    int prb_allocation;
    bool is_burst_mode;
} dynamic_allocation_t;

static dynamic_allocation_t ue_allocations[2] = {
    {0, 5, 10, MIN_PRB_ALLOCATION, false},  // UE1: mMTC
    {1, 6, 11, MIN_PRB_ALLOCATION, false}   // UE2: URLLC
};

static e2_node_arr_xapp_t g_nodes = {0};
static bool g_nodes_initialized = false;
static ue_id_e2sm_t stored_ue_ids[2] = {0};

static void init_csv_file(void) {
    csv_file = fopen("/home/tahanamjoo/kpm_rc_monitoring.csv", "w");
    if (csv_file == NULL) {
        perror("Failed to open CSV file");
        return;
    }
    
    fprintf(csv_file, "timestamp,indication_counter,latency_us,");
    fprintf(csv_file, "ue1_ngap_id,ue1_ran_ue_id,ue1_prb_dl,ue1_prb_ul,ue1_pdcp_dl_kb,ue1_pdcp_ul_kb,ue1_delay_dl_us,ue1_thp_dl_kbps,ue1_thp_ul_kbps,ue1_is_burst,ue1_prb_allocation,");
    fprintf(csv_file, "ue2_ngap_id,ue2_ran_ue_id,ue2_prb_dl,ue2_prb_ul,ue2_pdcp_dl_kb,ue2_pdcp_ul_kb,ue2_delay_dl_us,ue2_thp_dl_kbps,ue2_thp_ul_kbps,ue2_is_burst,ue2_prb_allocation,");
    fprintf(csv_file, "rc_drb_id,rc_qfi,rc_mapping_ind\n");
    fflush(csv_file);
    
    printf("[CSV]: Log file created at /home/tahanamjoo/kpm_rc_monitoring.csv\n");
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
        fprintf(csv_file, ",%lu,%lu,%d,%d,%d,%d,%.2f,%.2f,%.2f,%d,%d",
                ue_measurements[0].ue_ngap_id,
                ue_measurements[0].ran_ue_id,
                ue_measurements[0].prb_tot_dl,
                ue_measurements[0].prb_tot_ul,
                ue_measurements[0].pdcp_volume_dl,
                ue_measurements[0].pdcp_volume_ul,
                ue_measurements[0].rlc_delay_dl,
                ue_measurements[0].ue_thp_dl,
                ue_measurements[0].ue_thp_ul,
                ue_measurements[0].is_burst,
                ue_allocations[0].prb_allocation);
    } else {
        fprintf(csv_file, ",0,0,0,0,0,0,0.0,0.0,0.0,0,0");
    }
    
    if (num_ues >= 2) {
        fprintf(csv_file, ",%lu,%lu,%d,%d,%d,%d,%.2f,%.2f,%.2f,%d,%d",
                ue_measurements[1].ue_ngap_id,
                ue_measurements[1].ran_ue_id,
                ue_measurements[1].prb_tot_dl,
                ue_measurements[1].prb_tot_ul,
                ue_measurements[1].pdcp_volume_dl,
                ue_measurements[1].pdcp_volume_ul,
                ue_measurements[1].rlc_delay_dl,
                ue_measurements[1].ue_thp_dl,
                ue_measurements[1].ue_thp_ul,
                ue_measurements[1].is_burst,
                ue_allocations[1].prb_allocation);
    } else {
        fprintf(csv_file, ",0,0,0,0,0,0,0.0,0.0,0.0,0,0");
    }
    
    fprintf(csv_file, ",%d,%d,%d\n", rc_alloc.drb_id, rc_alloc.qfi, rc_alloc.mapping_ind);
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
    NULL, NULL, NULL, NULL,
};

static void log_int_value(byte_array_t name, meas_record_lst_t meas_record, ue_measurement_t* meas) {
    int value = meas_record.int_val / 12; // Assume reported value is subcarriers
    if (cmp_str_ba("RRU.PrbTotDl", name) == 0) {
        if (value > TOTAL_PRB_POOL) {
            printf("Warning: RRU.PrbTotDl = %d exceeds TOTAL_PRB_POOL (%d), setting to 0\n", value, TOTAL_PRB_POOL);
            value = 0;
        }
        printf("RRU.PrbTotDl = %d [PRBs]\n", value);
        if (meas) meas->prb_tot_dl = value;
    } else if (cmp_str_ba("RRU.PrbTotUl", name) == 0) {
        if (value > TOTAL_PRB_POOL) {
            printf("Warning: RRU.PrbTotUl = %d exceeds TOTAL_PRB_POOL (%d), setting to 0\n", value, TOTAL_PRB_POOL);
            value = 0;
        }
        printf("RRU.PrbTotUl = %d [PRBs]\n", value);
        if (meas) meas->prb_tot_ul = value;
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

static bool analyze_and_allocate_resources(void) {
    bool resource_reallocation_needed = false;
    
    for (size_t i = 0; i < num_ues; i++) {
        bool current_burst = ue_measurements[i].is_burst;
        bool previous_burst = ue_allocations[i].is_burst_mode;
        
        // Check if PRB values are valid
        if (ue_measurements[i].prb_tot_dl > TOTAL_PRB_POOL || ue_measurements[i].prb_tot_ul > TOTAL_PRB_POOL) {
            printf("[RESOURCE MANAGER]: Invalid PRB values for UE%zu, skipping allocation\n", i+1);
            continue;
        }
        
        // Detect transition to burst mode
        if (current_burst && !previous_burst) {
            printf("\n[RESOURCE MANAGER]: UE%zu entering BURST mode (RAN UE ID: %lu)\n", 
                   i+1, ue_measurements[i].ran_ue_id);
            
            ue_allocations[i].prb_allocation = BURST_PRB_ALLOCATION;
            ue_allocations[i].is_burst_mode = true;
            ue_allocations[i].drb_id = 6;  // URLLC DRB
            ue_allocations[i].qfi = 11;
            
            size_t other_ue = (i == 0) ? 1 : 0;
            if (other_ue < num_ues) {
                ue_allocations[other_ue].prb_allocation = TOTAL_PRB_POOL - BURST_PRB_ALLOCATION;
                ue_allocations[other_ue].drb_id = 5;  // mMTC DRB
                ue_allocations[other_ue].qfi = 10;
                printf("[RESOURCE MANAGER]: UE%zu reduced to %d PRBs to accommodate burst\n", 
                       other_ue+1, ue_allocations[other_ue].prb_allocation);
            }
            
            resource_reallocation_needed = true;
        }
        // Detect transition from burst to normal
        else if (!current_burst && previous_burst) {
            printf("\n[RESOURCE MANAGER]: UE%zu exiting BURST mode (RAN UE ID: %lu)\n", 
                   i+1, ue_measurements[i].ran_ue_id);
            
            ue_allocations[i].prb_allocation = NORMAL_PRB_ALLOCATION;
            ue_allocations[i].is_burst_mode = false;
            ue_allocations[i].drb_id = 5;  // mMTC DRB
            ue_allocations[i].qfi = 10;
            
            size_t other_ue = (i == 0) ? 1 : 0;
            if (other_ue < num_ues) {
                ue_allocations[other_ue].prb_allocation = NORMAL_PRB_ALLOCATION;
                ue_allocations[other_ue].drb_id = 5;  // mMTC DRB
                ue_allocations[other_ue].qfi = 10;
                printf("[RESOURCE MANAGER]: UE%zu restored to %d PRBs\n", 
                       other_ue+1, NORMAL_PRB_ALLOCATION);
            }
            
            resource_reallocation_needed = true;
        }
    }
    
    return resource_reallocation_needed;
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
            
            free_ue_id_e2sm(&ue_id);
            ue_id = cp_ue_id_e2sm(&ue_id_e2sm);
            
            free_ue_id_e2sm(&stored_ue_ids[i]);
            stored_ue_ids[i] = cp_ue_id_e2sm(&ue_id_e2sm);

            log_kpm_measurements(&msg_frm_3->meas_report_per_ue[i].ind_msg_format_1, &ue_measurements[i]);
        }
        
        for (size_t i = 0; i < num_ues; i++) {
            float thp_ul = ue_measurements[i].ue_thp_ul;
            if (thp_ul > BURST_DETECTION_THRESHOLD) {
                ue_measurements[i].is_burst = 1;
                printf("\n[BURST DETECTION]: UE%zu (RAN UE ID %lu) - Thp UL: %.2f kbps\n", 
                       i+1, ue_measurements[i].ran_ue_id, thp_ul);
            } else {
                ue_measurements[i].is_burst = 0;
            }
        }
        
        bool reallocation_needed = analyze_and_allocate_resources();
        
        if (reallocation_needed) {
            printf("\n[TRIGGER]: Resource reallocation required\n");
        }
        
        for (size_t i = 0; i < num_ues; i++) {
            rc_alloc.drb_id = ue_allocations[i].drb_id;
            rc_alloc.qfi = ue_allocations[i].qfi;
            rc_alloc.mapping_ind = 1;
        }
        
        log_to_csv(now, counter, latency);
        counter++;
    }
}

typedef enum {
    DRB_QoS_Configuration_7_6_2_1 = 1,
    QoS_flow_mapping_configuration_7_6_2_1 = 2,
    Logical_channel_configuration_7_6_2_1 = 3,
    Radio_admission_control_7_6_2_1 = 4,
    DRB_termination_control_7_6_2_1 = 5,
    DRB_split_ratio_control_7_6_2_1 = 6,
    PDCP_Duplication_control_7_6_2_1 = 7,
} rc_ctrl_service_style_1_e;

typedef enum {
    DRB_ID_8_4_2_2 = 1,
    LIST_OF_QOS_FLOWS_MOD_IN_DRB_8_4_2_2 = 2,
    QOS_FLOW_ITEM_8_4_2_2 = 3,
    QOS_FLOW_ID_8_4_2_2 = 4,
    QOS_FLOW_MAPPING_IND_8_4_2_2 = 5,
} qos_flow_mapping_conf_e;

static seq_ran_param_t fill_drb_id_param_dynamic(int drb_id) {
    seq_ran_param_t drb_param = {0};
    drb_param.ran_param_id = DRB_ID_8_4_2_2;
    drb_param.ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
    drb_param.ran_param_val.flag_true = calloc(1, sizeof(ran_parameter_value_t));
    assert(drb_param.ran_param_val.flag_true != NULL && "Memory exhausted");
    drb_param.ran_param_val.flag_true->type = INTEGER_RAN_PARAMETER_VALUE;
    drb_param.ran_param_val.flag_true->int_ran = drb_id;
    printf("Allocating DRB ID = %d\n", drb_id);
    return drb_param;
}

static seq_ran_param_t fill_qos_flows_param_dynamic(int qfi, int mapping_ind) {
    seq_ran_param_t qos_param = {0};
    qos_param.ran_param_id = LIST_OF_QOS_FLOWS_MOD_IN_DRB_8_4_2_2;
    qos_param.ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
    qos_param.ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
    assert(qos_param.ran_param_val.lst != NULL && "Memory exhausted");
    ran_param_list_t* rpl = qos_param.ran_param_val.lst;
    rpl->sz_lst_ran_param = 1;
    rpl->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
    assert(rpl->lst_ran_param != NULL && "Memory exhausted");
    rpl->lst_ran_param[0].ran_param_struct.sz_ran_param_struct = 2;
    rpl->lst_ran_param[0].ran_param_struct.ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
    assert(rpl->lst_ran_param[0].ran_param_struct.ran_param_struct != NULL && "Memory exhausted");
    seq_ran_param_t* rps = rpl->lst_ran_param[0].ran_param_struct.ran_param_struct;
    rps[0].ran_param_id = QOS_FLOW_ID_8_4_2_2;
    rps[0].ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
    rps[0].ran_param_val.flag_true = calloc(1, sizeof(ran_parameter_value_t));
    assert(rps[0].ran_param_val.flag_true != NULL && "Memory exhausted");
    rps[0].ran_param_val.flag_true->type = INTEGER_RAN_PARAMETER_VALUE;
    rps[0].ran_param_val.flag_true->int_ran = qfi;
    printf("Allocating QFI = %d\n", qfi);
    rps[1].ran_param_id = QOS_FLOW_MAPPING_IND_8_4_2_2;
    rps[1].ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
    rps[1].ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(rpl->lst_ran_param[0].ran_param_struct.ran_param_struct != NULL && "Memory exhausted");
    rps[1].ran_param_val.flag_false->type = INTEGER_RAN_PARAMETER_VALUE;
    rps[1].ran_param_val.flag_false->int_ran = mapping_ind;
    printf("Allocating Mapping Ind = %d (UL)\n", mapping_ind);
    return qos_param;
}

static void fill_rc_ctrl_act_dynamic(seq_ctrl_act_2_t const* ctrl_act,
                                    size_t const sz,
                                    e2sm_rc_ctrl_hdr_frmt_1_t* hdr,
                                    e2sm_rc_ctrl_msg_frmt_1_t* msg,
                                    int ue_idx) {
    assert(ctrl_act != NULL);
    for (size_t i = 0; i < sz; i++) {
        assert(cmp_str_ba("QoS flow mapping configuration", ctrl_act[i].name) == 0 && "Add requested CONTROL Action");
        hdr->ctrl_act_id = QoS_flow_mapping_configuration_7_6_2_1;
        msg->sz_ran_param = ctrl_act[i].sz_seq_assoc_ran_param;
        assert(msg->sz_ran_param == 2);
        msg->ran_param = calloc(msg->sz_ran_param, sizeof(seq_ran_param_t));
        assert(msg->ran_param != NULL && "Memory exhausted");
        assert(ctrl_act[i].assoc_ran_param[0].id == DRB_ID_8_4_2_2);
        msg->ran_param[0] = fill_drb_id_param_dynamic(ue_allocations[ue_idx].drb_id);
        assert(ctrl_act[i].assoc_ran_param[1].id == LIST_OF_QOS_FLOWS_MOD_IN_DRB_8_4_2_2);
        msg->ran_param[1] = fill_qos_flows_param_dynamic(ue_allocations[ue_idx].qfi, 1);
    }
}

static rc_ctrl_req_data_t gen_rc_ctrl_msg_for_ue(ran_func_def_ctrl_t const* ran_func, 
                                                ue_id_e2sm_t* target_ue_id,
                                                int ue_idx) {
    assert(ran_func != NULL);
    rc_ctrl_req_data_t rc_ctrl = {0};
    for (size_t i = 0; i < ran_func->sz_seq_ctrl_style; i++) {
        assert(cmp_str_ba("Radio Bearer Control", ran_func->seq_ctrl_style[i].name) == 0 && "Add requested CONTROL Style");
        rc_ctrl.hdr.format = ran_func->seq_ctrl_style[i].hdr;
        assert(rc_ctrl.hdr.format == FORMAT_1_E2SM_RC_CTRL_HDR && "Indication Header Format received not valid");
        rc_ctrl.hdr.frmt_1.ric_style_type = 1;
        rc_ctrl.hdr.frmt_1.ue_id = cp_ue_id_e2sm(target_ue_id);
        rc_ctrl.msg.format = ran_func->seq_ctrl_style[i].msg;
        assert(rc_ctrl.msg.format == FORMAT_1_E2SM_RC_CTRL_MSG && "Indication Message Format received not valid");
        fill_rc_ctrl_act_dynamic(ran_func->seq_ctrl_style[i].seq_ctrl_act,
                                ran_func->seq_ctrl_style[i].sz_seq_ctrl_act,
                                &rc_ctrl.hdr.frmt_1,
                                &rc_ctrl.msg.frmt_1,
                                ue_idx);
    }
    return rc_ctrl;
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
    NULL, NULL, NULL, fill_report_style_4, NULL,
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

static void* rc_control_thread(void* arg) {
    (void)arg;
    const int RC_ran_function = 3;
    static bool previous_burst_state[2] = {false, false};
    
    while (1) {
        sleep(1);
        if (!g_nodes_initialized) continue;
        
        bool current_state_changed = false;
        {
            lock_guard(&mtx);
            for (size_t i = 0; i < num_ues && i < 2; i++) {
                if (ue_allocations[i].is_burst_mode != previous_burst_state[i]) {
                    current_state_changed = true;
                    previous_burst_state[i] = ue_allocations[i].is_burst_mode;
                }
            }
        }
        
        if (current_state_changed) {
            printf("\n[RC CONTROL THREAD]: Burst state changed, sending RC controls\n");
            
            for (size_t node_idx = 0; node_idx < g_nodes.len; ++node_idx) {
                e2_node_connected_xapp_t* n = &g_nodes.n[node_idx];
                size_t const idx = find_sm_idx(n->rf, n->len_rf, eq_sm, RC_ran_function);
                if (n->rf[idx].defn.type == RC_RAN_FUNC_DEF_E && 
                    n->rf[idx].defn.rc.ctrl != NULL) {
                    
                    for (size_t ue_idx = 0; ue_idx < num_ues && ue_idx < 2; ue_idx++) {
                        // Skip if PRB values are invalid
                        if (ue_measurements[ue_idx].prb_tot_dl > TOTAL_PRB_POOL || 
                            ue_measurements[ue_idx].prb_tot_ul > TOTAL_PRB_POOL) {
                            printf("[RC CONTROL]: Skipping UE%zu due to invalid PRB values\n", ue_idx+1);
                            continue;
                        }
                        
                        ue_id_e2sm_t target_ue_id;
                        {
                            lock_guard(&mtx);
                            target_ue_id = cp_ue_id_e2sm(&stored_ue_ids[ue_idx]);
                        }
                        
                        rc_ctrl_req_data_t rc_ctrl = gen_rc_ctrl_msg_for_ue(
                            n->rf[idx].defn.rc.ctrl, 
                            &target_ue_id,
                            ue_idx
                        );
                        
                        printf("[RC CONTROL]: Sending control for UE%zu - DRB:%d, QFI:%d, PRB:%d\n",
                               ue_idx+1,
                               ue_allocations[ue_idx].drb_id,
                               ue_allocations[ue_idx].qfi,
                               ue_allocations[ue_idx].prb_allocation);
                        
                        control_sm_xapp_api(&n->id, RC_ran_function, &rc_ctrl);
                        
                        free_rc_ctrl_req_data(&rc_ctrl);
                        free_ue_id_e2sm(&target_ue_id);
                    }
                }
            }
        }
    }
    
    return NULL;
}

int main(int argc, char* argv[]) {
    fr_args_t args = init_fr_args(argc, argv);
    init_xapp_api(&args);
    sleep(1);

    g_nodes = e2_nodes_xapp_api();
    assert(g_nodes.len > 0);
    g_nodes_initialized = true;

    printf("[KPM RC]: Connected E2 nodes = %d\n", g_nodes.len);
    printf("[KPM RC]: Total PRB pool = %d\n", TOTAL_PRB_POOL);

    pthread_mutexattr_t attr = {0};
    int rc = pthread_mutex_init(&mtx, &attr);
    assert(rc == 0);

    init_csv_file();

    sm_ans_xapp_t* hndl = calloc(g_nodes.len, sizeof(sm_ans_xapp_t));
    assert(hndl != NULL);

    int const KPM_ran_function = 2;

    for (size_t i = 0; i < g_nodes.len; ++i) {
        e2_node_connected_xapp_t* n = &g_nodes.n[i];
        size_t const idx = find_sm_idx(n->rf, n->len_rf, eq_sm, KPM_ran_function);
        assert(n->rf[idx].defn.type == KPM_RAN_FUNC_DEF_E && "KPM is not the received RAN Function");
        if (n->rf[idx].defn.kpm.ric_report_style_list != NULL) {
            kpm_sub_data_t kpm_sub = gen_kpm_subs(&n->rf[idx].defn.kpm);
            hndl[i] = report_sm_xapp_api(&n->id, KPM_ran_function, &kpm_sub, sm_cb_kpm);
            assert(hndl[i].success == true);
            free_kpm_sub_data(&kpm_sub);
        }
    }

    pthread_t rc_thread;
    rc = pthread_create(&rc_thread, NULL, rc_control_thread, NULL);
    assert(rc == 0);
    
    printf("[MAIN]: RC control thread started\n");

    xapp_wait_end_api();

    for (int i = 0; i < g_nodes.len; ++i) {
        if (hndl[i].success == true)
            rm_report_sm_xapp_api(hndl[i].u.handle);
    }
    free(hndl);

    close_csv_file();

    while (try_stop_xapp_api() == false)
        usleep(1000);

    free_e2_node_arr_xapp(&g_nodes);

    rc = pthread_mutex_destroy(&mtx);
    assert(rc == 0);

    printf("[KPM RC]: Test xApp run SUCCESSFULLY\n");
    
    return 0;
}


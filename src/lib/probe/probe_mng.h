/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: luzhihao
 * Create: 2023-04-06
 * Description: probe management
 ******************************************************************************/
#ifndef __PROBE_MNG_H__
#define __PROBE_MNG_H__

#pragma once
#include <pthread.h>
#include <stdbool.h>

#include "base.h"
#include "fifo.h"
#include "ipc.h"
#include "ext_label.h"

#include "args.h"

#define PROBE_FLAGS_STARTED         0x00000001    // probe has been tried to start by user
#define PROBE_FLAGS_STOPPED         0x00000002    // probe is in stopped state
#define PROBE_FLAGS_STOPPING        0x00000004    // probe has been tried to stop by user
#define PROBE_FLAGS_RUNNING         0x00000008    // probe is in running state

#define PROBE_STATE_RUNNING         "running"
#define PROBE_STATE_STOPPED         "stopped"
#define PROBE_STATE_UNKNOWN         "unknown"

#define PARSE_JSON_ERR_STR_LEN 200
extern char g_parse_json_err[PARSE_JSON_ERR_STR_LEN];

struct probe_define_s {
    char *desc;
    char *bin;
    enum probe_type_e type;
    u8 snooper_type;
    char enable;
};

struct probe_threshold_verify {
    char *name;
    u32 max;
};
typedef int (*ParseParam)(const char*, struct probe_params *);
struct param_define_s {
    char *desc;
    ParseParam parser;
};

struct probe_status_s {
    u32 status_flags;                                   // Refer to flags defined [PROBE_FLAGS_XXX]
};

struct custom {
    u32 index;
    bool privilege;
    struct custom_ipc custom_ipc_msg;
};

struct custom_ini {
    struct probe_s *custom[MAX_CUSTOM_NUM];   //The probe number starts from subscript 1.
    u32 custom_num;
};

struct probe_s;
typedef int (*ProbeMain)(struct probe_s *);
typedef void *(*ProbeCB)(void *);
struct probe_s {
    char *name;                                         // Name for probe
    char *bin;                                          // Execute bin file for probe
    char is_extend_probe;                               // 0: Native probe, otherwise extend probe
    char is_params_chg;                                 // Probe params changed
    char is_snooper_chg;                                // Probe snoopers changed
    char resnd_snooper_for_restart;                     // Need to resend snooper obj after probe is restarted
    u8 snooper_type;                                    // Specify the type of snoopers that one probe really concern */
    enum probe_type_e probe_type;
    struct custom custom;                               // User-defined probe
    u32 probe_range_flags;                              // Refer to flags defined [PROBE_RANGE_XX_XX]
    ProbeMain probe_entry;                              // Main function for native probe
    ProbeCB cb;                                         // Thread cb for probe
    Fifo *fifo;                                         // Data channel for probe, !!!NOTICE: context in probe-thread
    pthread_t tid;                                      // Thread for admin probe

    int pid;                                            // PID of extend probe process(Invalid value -1), wr&rd by rest/probe/probe-mng thread
    struct probe_status_s probe_status;                 // Status of probe, wr&rd by rest/probe/probe-mng thread
    pthread_rwlock_t rwlock;                            // Used to exclusive operations between multi-thread.

    u32 snooper_conf_num;
    struct snooper_conf_s *snooper_confs[SNOOPER_CONF_MAX];  // snooper config, wr&rd by rest/probe-mng thread
    struct snooper_obj_s *snooper_objs[SNOOPER_MAX];         // snooper object, wr&rd by rest/probe-mng thread

    struct probe_params probe_param;                    // params for probe
    struct ext_label_conf ext_label_conf;
};

struct probe_mng_s {
    int msq_id;                                         // ipc control msg channel
    struct probe_s *probes[PROBE_TYPE_MAX];
    struct probe_s *custom[MAX_CUSTOM_NUM];           //Used to manage external probes.
    u32 custom_index;                                 //Used to define external probes index.

    void *snooper_skel;
    const char *btf_custom_path;
    void *snooper_proc_pb;                              // context in perf event
    void *snooper_cgrp_pb;                              // context in perf event
    int ingress_epoll_fd;                               // !!!NOTICE: NO NEED FREE.
    pthread_t tid;                                      // probe mng thread, used to poll perf event
    time_t keeplive_ts;                                 // Used to keeplive probe

    pthread_rwlock_t rwlock;                            // Exclusive operations between rest-api event and perf event.

};

#define IS_EXTEND_PROBE(probe)  (probe->is_extend_probe)
#define IS_NATIVE_PROBE(probe)  (!probe->is_extend_probe)

void *extend_probe_thread_cb(void *arg);
void *native_probe_thread_cb(void *arg);

int lkup_and_set_probe_pid(struct probe_s *probe);
void run_probe_mng_daemon(struct probe_mng_s *probe_mng);
int parse_probe_json(const char *probe_name, const char *probe_content);
char *get_probe_json(const char *probe_name);
struct probe_mng_s *create_probe_mng(void);
void destroy_probe_mng(void);
void destroy_custom_ini(void);
void destroy_probe_threads(void);
u32 get_probe_status_flags(struct probe_s* probe);
void set_probe_status_stopped(struct probe_s* probe);
void set_probe_pid(struct probe_s *probe, int pid);
int check_custom_range(const char *key, const char *comp);
int file_handle(const char *file, char *data, size_t size);
char *probe_extern_cmd_param(const char *key, const void *item);
int is_file_exist(char *dir);

#define IS_STOPPED_PROBE(probe)      (get_probe_status_flags(probe) & PROBE_FLAGS_STOPPED)
#define IS_STARTED_PROBE(probe)      (get_probe_status_flags(probe) & PROBE_FLAGS_STARTED)
#define IS_STOPPING_PROBE(probe)     (get_probe_status_flags(probe) & PROBE_FLAGS_STOPPING)
#define IS_RUNNING_PROBE(probe)      (get_probe_status_flags(probe) & PROBE_FLAGS_RUNNING)

#define PARSE_ERR(fmt, ...) \
    (void)snprintf(g_parse_json_err, sizeof(g_parse_json_err), (fmt), ##__VA_ARGS__)

#endif

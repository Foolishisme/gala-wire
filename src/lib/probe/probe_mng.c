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
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/epoll.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <dirent.h>
#include <pwd.h>

#include "ipc.h"
#include "pod_mng.h"
#include "snooper.h"
#include "__compat.h"
#include "probe_params_parser.h"
#include "json_tool.h"
#include "probe_mng.h"

static int init_probe_bin(struct probe_s *probe, enum probe_type_e probe_type);

struct probe_define_s probe_define[] = {
    {"baseinfo",      "system_infos",                                      PROBE_BASEINFO,    SNOOPER_TYPE_ALL,    ENABLE_BASEINFO},
    {"virt",          "virtualized_infos",                                 PROBE_VIRT,        SNOOPER_TYPE_NONE,   ENABLE_VIRT},
    {"flamegraph",    "/opt/gala-gopher/extend_probes/stackprobe",         PROBE_FG,          SNOOPER_TYPE_PROC,   ENABLE_FLAMEGRAPH},
    {"l7",            "/opt/gala-gopher/extend_probes/l7probe",            PROBE_L7,          SNOOPER_TYPE_ALL,    ENABLE_L7},
    {"tcp",           "/opt/gala-gopher/extend_probes/tcpprobe",           PROBE_TCP,         SNOOPER_TYPE_PROC,   ENABLE_TCP},
    {"socket",        "/opt/gala-gopher/extend_probes/endpoint",           PROBE_SOCKET,      SNOOPER_TYPE_ALL,    ENABLE_SOCKET},
    {"io",            "/opt/gala-gopher/extend_probes/ioprobe",            PROBE_IO,          SNOOPER_TYPE_NONE,   ENABLE_IO},
    {"proc",          "/opt/gala-gopher/extend_probes/taskprobe",          PROBE_PROC,        SNOOPER_TYPE_ALL,    ENABLE_PROC},
    {"jvm",           "/opt/gala-gopher/extend_probes/jvmprobe",           PROBE_JVM,         SNOOPER_TYPE_PROC,   ENABLE_JVM},
    {"postgre_sli",   "/opt/gala-gopher/extend_probes/pgsliprobe",         PROBE_POSTGRE_SLI, SNOOPER_TYPE_NONE,   ENABLE_POSTGRE_SLI},
    {"opengauss_sli", "/opt/gala-gopher/extend_probes/pg_stat_probe.py",   PROBE_GAUSS_SLI,   SNOOPER_TYPE_NONE,   ENABLE_OPENGAUSS_SLI},
    {"nginx",         "/opt/gala-gopher/extend_probes/nginx_probe",        PROBE_NGINX,       SNOOPER_TYPE_NONE,   ENABLE_NGINX},
    {"lvs",           "/opt/gala-gopher/extend_probes/trace_lvs",          PROBE_LVS,         SNOOPER_TYPE_NONE,   ENABLE_LVS},
    {"kafka",         "/opt/gala-gopher/extend_probes/kafkaprobe",         PROBE_KAFKA,       SNOOPER_TYPE_NONE,   ENABLE_KAFKA},
    {"tprofiling",    "/opt/gala-gopher/extend_probes/tprofiling",         PROBE_TP,          SNOOPER_TYPE_PROC,   ENABLE_TPROFILING},
    {"hw",            "/opt/gala-gopher/extend_probes/hwprobe",            PROBE_HW,          SNOOPER_TYPE_NONE,   ENABLE_HW},
    {"ksli",          "/opt/gala-gopher/extend_probes/ksliprobe",          PROBE_KSLI,        SNOOPER_TYPE_NONE,   ENABLE_KSLI},
    {"container",     "/opt/gala-gopher/extend_probes/cadvisor_probe.py",  PROBE_CONTAINER,   SNOOPER_TYPE_PROC,   ENABLE_CONTAINER},
    {"sermant",       "/opt/gala-gopher/extend_probes/sermant_probe.py",   PROBE_SERMANT,     SNOOPER_TYPE_PROC,   ENABLE_SERMANT},
    {"sli",           "/opt/gala-gopher/extend_probes/sliprobe",           PROBE_SLI,         SNOOPER_TYPE_CON,    ENABLE_SLI},
    {"flowtracer",    "/opt/gala-gopher/extend_probes/flowtracer",         PROBE_FLOWTRACER,  SNOOPER_TYPE_NONE,   ENABLE_FLOWTRACER}
    // If you want to add a probe, add the probe define.
};

struct probe_threshold_verify verify[] = {
    {"name",       MAX_CUSTOM_NAME_LEN},
    {"bin",        MAX_BIN_LEN},
    {"probe_num",  MAX_SUBPROBE_NUM},
    {"param",      MAX_CUSTOM_PARAMS_LEN}
};

struct probe_range_define_s {
    enum probe_type_e probe_type;
    char *desc;
    u32 flags;                      /* Refer to [PROBE] subprobe define. */
};

struct probe_range_define_s probe_range_define[] = {
    {PROBE_FG,     "oncpu",               PROBE_RANGE_ONCPU},
    {PROBE_FG,     "offcpu",              PROBE_RANGE_OFFCPU},
    {PROBE_FG,     "mem",                 PROBE_RANGE_MEM},
    {PROBE_FG,     "mem_glibc",           PROBE_RANGE_MEM_GLIBC},
    {PROBE_FG,     "io",                  PROBE_RANGE_IO},

    {PROBE_L7,     "l7_bytes_metrics",    PROBE_RANGE_L7BYTES_METRICS},
    {PROBE_L7,     "l7_rpc_metrics",      PROBE_RANGE_L7RPC_METRICS},
    {PROBE_L7,     "l7_rpc_trace",        PROBE_RANGE_L7RPC_TRACE},

    {PROBE_TCP,    "tcp_abnormal",        PROBE_RANGE_TCP_ABNORMAL},
    {PROBE_TCP,    "tcp_rtt",             PROBE_RANGE_TCP_RTT},
    {PROBE_TCP,    "tcp_windows",         PROBE_RANGE_TCP_WINDOWS},
    {PROBE_TCP,    "tcp_srtt",            PROBE_RANGE_TCP_SRTT},
    {PROBE_TCP,    "tcp_rate",            PROBE_RANGE_TCP_RATE},
    {PROBE_TCP,    "tcp_sockbuf",         PROBE_RANGE_TCP_SOCKBUF},
    {PROBE_TCP,    "tcp_stats",           PROBE_RANGE_TCP_STATS},
    {PROBE_TCP,    "tcp_delay",           PROBE_RANGE_TCP_DELAY},

    {PROBE_SOCKET, "tcp_socket",          PROBE_RANGE_SOCKET_TCP},
    {PROBE_SOCKET, "udp_socket",          PROBE_RANGE_SOCKET_UDP},

    {PROBE_IO,     "io_trace",            PROBE_RANGE_IO_TRACE},
    {PROBE_IO,     "io_err",              PROBE_RANGE_IO_ERR},
    {PROBE_IO,     "io_count",            PROBE_RANGE_IO_COUNT},
    {PROBE_IO,     "page_cache",          PROBE_RANGE_IO_PAGECACHE},

    {PROBE_PROC,   "proc_syscall",        PROBE_RANGE_PROC_SYSCALL},
    {PROBE_PROC,   "proc_fs",             PROBE_RANGE_PROC_FS},
    {PROBE_PROC,   "proc_dns",            PROBE_RANGE_PROC_DNS},
    {PROBE_PROC,   "proc_io",             PROBE_RANGE_PROC_IO},
    {PROBE_PROC,   "proc_pagecache",      PROBE_RANGE_PROC_PAGECACHE},
    {PROBE_PROC,   "proc_net",            PROBE_RANGE_PROC_NET},
    {PROBE_PROC,   "proc_offcpu",         PROBE_RANGE_PROC_OFFCPU},
    {PROBE_PROC,   "proc_ioctl",          PROBE_RANGE_PROC_IOCTL},
    {PROBE_PROC,   "proc_pygc",           PROBE_RANGE_PROC_PYGC},

    {PROBE_BASEINFO,  "cpu",              PROBE_RANGE_SYS_CPU},
    {PROBE_BASEINFO,  "mem",              PROBE_RANGE_SYS_MEM},
    {PROBE_BASEINFO,  "nic",              PROBE_RANGE_SYS_NIC},
    {PROBE_BASEINFO,  "net",              PROBE_RANGE_SYS_NET},
    {PROBE_BASEINFO,  "disk",             PROBE_RANGE_SYS_DISK},
    {PROBE_BASEINFO,  "fs",               PROBE_RANGE_SYS_FS},
    {PROBE_BASEINFO,  "proc",             PROBE_RANGE_SYS_PROC},
    {PROBE_BASEINFO,  "host",             PROBE_RANGE_SYS_HOST},
    {PROBE_BASEINFO,  "con",              PROBE_RANGE_SYS_CON},

    {PROBE_TP,     "oncpu",               PROBE_RANGE_TPROFILING_ONCPU},
    {PROBE_TP,     "syscall_file",        PROBE_RANGE_TPROFILING_SYSCALL_FILE},
    {PROBE_TP,     "syscall_net",         PROBE_RANGE_TPROFILING_SYSCALL_NET},
    {PROBE_TP,     "syscall_lock",        PROBE_RANGE_TPROFILING_SYSCALL_LOCK},
    {PROBE_TP,     "syscall_sched",       PROBE_RANGE_TPROFILING_SYSCALL_SCHED},
    {PROBE_TP,     "python_gc",           PROBE_RANGE_TPROFILING_PYTHON_GC},
    {PROBE_TP,     "pthread_sync",        PROBE_RANGE_TPROFILING_PTHREAD_SYNC},
    {PROBE_TP,     "oncpu_sample",        PROBE_RANGE_TPROFILING_ONCPU_SAMPLE},
    {PROBE_TP,     "mem_usage",           PROBE_RANGE_TPROFILING_MEM_USAGE},
    {PROBE_TP,     "mem_glibc",           PROBE_RANGE_TPROFILING_MEM_GLIBC},
    {PROBE_TP,     "offcpu",              PROBE_RANGE_TPROFILING_OFFCPU},

    {PROBE_HW,     "hw_nic",              PROBE_RANGE_HW_NIC},
    {PROBE_HW,     "hw_mem",              PROBE_RANGE_HW_MEM},

    {PROBE_SERMANT, "l7_bytes_metrics", PROBE_RANGE_SERMANT_BYTES_METRICS},
    {PROBE_SERMANT, "l7_rpc_metrics",   PROBE_RANGE_SERMANT_RPC_METRICS},
    {PROBE_SERMANT, "l7_rpc_trace",     PROBE_RANGE_SERMANT_RPC_TRACE},

    {PROBE_SLI,     "cpu",              PROBE_RANGE_SLI_CPU},
    {PROBE_SLI,     "mem",              PROBE_RANGE_SLI_MEM},
    {PROBE_SLI,     "io",               PROBE_RANGE_SLI_IO},
    {PROBE_SLI,     "node",             PROBE_RANGE_SLI_NODE},
    {PROBE_SLI,     "container",        PROBE_RANGE_SLI_CONTAINER},
    {PROBE_SLI,     "histogram",        PROBE_RANGE_SLI_HISTOGRAM_METRICS},
    // If you want to add a probe, add the probe range.
};

#define EXTEND_PROBE_PROCID_CMD  "pgrep -g %d -af \"%s$\" | awk '{print $1}'"
int lkup_and_set_probe_pid(struct probe_s *probe)
{
    int pid;
    char cmd[COMMAND_LEN];
    char pid_str[INT_LEN];

    if (probe->bin == NULL) {
        return -1;
    }

    cmd[0] = 0;
    (void)snprintf(cmd, COMMAND_LEN, EXTEND_PROBE_PROCID_CMD, getpid(), probe->bin);
    if (exec_cmd((const char *)cmd, pid_str, INT_LEN) < 0) {
        return -1;
    }
    pid = strtol(pid_str, NULL, 10);
    if (pid == getpid() || pid <= 0) {
        return -1;
    }
    set_probe_pid(probe, pid);
    return 0;
}

static int get_probe_range(enum probe_type_e probe_type, const char *range)
{

    size_t size = sizeof(probe_range_define) / sizeof(struct probe_range_define_s);

    for (size_t i = 0; i < size; i++) {
        if (probe_range_define[i].probe_type == probe_type && !strcasecmp(probe_range_define[i].desc, range)) {
            return probe_range_define[i].flags;
        }
    }

    return 0;
}

static int check_probe_range(struct probe_s *probe)
{
    if (probe->probe_range_flags == 0) {
        size_t size = sizeof(probe_range_define) / sizeof(struct probe_range_define_s);
        for (size_t i = 0; i < size; i++) {
            if (probe_range_define[i].probe_type == probe->probe_type) {
                PARSE_ERR("invalid probe ranges: at least one must be set");
                return -1;
            }
        }
    }
    return 0;
}

static int check_duplicate(const char str[][MAX_CUSTOM_NAME_LEN], const unsigned int n) {
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            if (strcmp(str[i], str[j]) == 0) {
                return -1;
            }
        }
    }
    return 0;
}


int check_custom_range(const char *key, const char *comp)
{
    if (key == NULL || comp == NULL) {
        return -1;
    }

    int size = sizeof(verify) / sizeof(struct probe_threshold_verify);
    for (int i = 0; i < size; i++) {
        if (strcmp(verify[i].name, comp) != 0) {
            continue;
        }
        if (strlen(key) == 0 || strlen(key) > verify[i].max) {
            return -1;
        }
    }
    return 0;
}

static int check_probe_snooper_conf_num(struct probe_s *probe)
{
    if (probe->snooper_conf_num == 0 && (!strcmp(probe->name, "tcp") ||
        !strcmp(probe->name, "socket") || !strcmp(probe->name, "container"))) {
        PARSE_ERR("the snooper for %s cannot be empty", probe->name);
        return -1;
    }
    return 0;
}

static struct probe_mng_s *g_probe_mng;
static struct custom_ini *g_custom_ini;

char g_parse_json_err[PARSE_JSON_ERR_STR_LEN];

void get_probemng_lock(void)
{
    (void)pthread_rwlock_wrlock(&g_probe_mng->rwlock);
}

void put_probemng_lock(void)
{
    (void)pthread_rwlock_unlock(&g_probe_mng->rwlock);
}

u32 get_probe_status_flags(struct probe_s* probe)
{
    u32 probe_status_flags;
    (void)pthread_rwlock_wrlock(&probe->rwlock);
    probe_status_flags = probe->probe_status.status_flags;
    (void)pthread_rwlock_unlock(&probe->rwlock);

    return probe_status_flags;
}

static void set_probe_status_started(struct probe_s* probe)
{
    (void)pthread_rwlock_wrlock(&probe->rwlock);
    probe->probe_status.status_flags |= PROBE_FLAGS_STARTED;
    probe->probe_status.status_flags &= ~(PROBE_FLAGS_STOPPING);
    (void)pthread_rwlock_unlock(&probe->rwlock);
}

void set_probe_status_stopped(struct probe_s* probe)
{
    (void)pthread_rwlock_wrlock(&probe->rwlock);
    probe->probe_status.status_flags |= PROBE_FLAGS_STOPPED;
    probe->probe_status.status_flags &= ~(PROBE_FLAGS_RUNNING);
    (void)pthread_rwlock_unlock(&probe->rwlock);
}

static void set_probe_status_stopping(struct probe_s* probe)
{
    (void)pthread_rwlock_wrlock(&probe->rwlock);
    probe->probe_status.status_flags |= PROBE_FLAGS_STOPPING;
    probe->probe_status.status_flags &= ~(PROBE_FLAGS_STARTED);
    (void)pthread_rwlock_unlock(&probe->rwlock);
}

static void set_probe_status_flags(struct probe_s* probe, u32 flags)
{
    (void)pthread_rwlock_wrlock(&probe->rwlock);
    probe->probe_status.status_flags |= flags;
    (void)pthread_rwlock_unlock(&probe->rwlock);
}

#if 0
static void unset_probe_status_flags(struct probe_s* probe, u32 flags)
{
    (void)pthread_rwlock_wrlock(&probe->rwlock);
    probe->probe_status.status_flags &= ~(flags);
    (void)pthread_rwlock_unlock(&probe->rwlock);
}
#endif

static int attach_probe_fd(struct probe_mng_s *probe_mng, struct probe_s *probe)
{
    int ret;
    struct epoll_event event;

    if (probe_mng->ingress_epoll_fd < 0) {
        return -1;
    }

    event.events = EPOLLIN;
    event.data.ptr = probe->fifo;

    ret = epoll_ctl(probe_mng->ingress_epoll_fd, EPOLL_CTL_ADD, probe->fifo->triggerFd, &event);
    if (ret) {
        ERROR("[PROBMNG] add EPOLLIN event failed, probe %s.\n", probe->name);
        return ret;
    }
    return 0;
}

static void detach_probe_fd(struct probe_mng_s *probe_mng, struct probe_s *probe)
{
    struct epoll_event event;
    if (probe_mng == NULL || probe == NULL) {
        return;
    }

    if (probe_mng->ingress_epoll_fd < 0 || probe->fifo == NULL) {
        return;
    }

    event.events = EPOLLIN;
    event.data.ptr = probe->fifo;

    (void)epoll_ctl(probe_mng->ingress_epoll_fd, EPOLL_CTL_DEL, probe->fifo->triggerFd, &event);
    return;
}

static void destroy_probe(struct probe_s *probe)
{
    u32 i;

    if (probe == NULL) {
        return;
    }

    if (probe->name) {
        free(probe->name);
        probe->name = NULL;
    }

    if (probe->bin) {
        free(probe->bin);
        probe->bin = NULL;
    }

    detach_probe_fd(g_probe_mng, probe);
    if (probe->fifo != NULL) {
        FifoDestroy(probe->fifo);
        probe->fifo = NULL;
    }

    for (i = 0 ; i < probe->snooper_conf_num ; i++) {
        free_snooper_conf(probe->snooper_confs[i]);
        probe->snooper_confs[i] = NULL;
    }

    for (i = 0; i < SNOOPER_MAX; i++) {
        free_snooper_obj(probe->snooper_objs[i]);
        probe->snooper_objs[i] = NULL;
    }

    probe->snooper_conf_num = 0;
    (void)pthread_rwlock_destroy(&probe->rwlock);

    destroy_ext_label_conf_locked(&probe->ext_label_conf);
    (void)pthread_rwlock_destroy(&probe->ext_label_conf.rwlock);

    free(probe);
    probe = NULL;
}

static struct probe_s* new_probe(const char* name, enum probe_type_e probe_type)
{
    int ret;
    struct probe_s *probe = NULL;

    probe = (struct probe_s *)malloc(sizeof(struct probe_s));
    if (probe == NULL) {
        return NULL;
    }

    memset(probe, 0, sizeof(struct probe_s));
    probe->name = strdup(name);
    if (!probe->name) {
        goto err;
    }

    ret = pthread_rwlock_init(&probe->rwlock, NULL);
    if (ret) {
        goto err;
    }
    ret = pthread_rwlock_init(&probe->ext_label_conf.rwlock, NULL);
    if (ret) {
        goto err;
    }

    probe->fifo = FifoCreate(MAX_FIFO_SIZE);
    if (probe->fifo == NULL) {
        goto err;
    }
    probe->fifo->probe = probe;
    probe->probe_type = probe_type;
    probe->snooper_type = probe_define[probe_type - 1].snooper_type;
    ret = init_probe_bin(probe, probe_type);
    if (ret) {
        goto err;
    }
    set_default_params(probe);

    ret = attach_probe_fd(g_probe_mng, probe);
    if (ret) {
        goto err;
    }

    set_probe_status_flags(probe, PROBE_FLAGS_STOPPED);
    probe->pid = -1;

    return probe;

err:
    destroy_probe(probe);
    return NULL;
}

static int set_probe_entry(struct probe_s *probe)
{
    int ret = 0;
    char entry_str[MAX_PROBE_NAME_LEN];

    if (probe->probe_entry != NULL) {
        return 0;
    }

    void *hdl = dlopen(NULL, RTLD_NOW | RTLD_GLOBAL);
    if (hdl == NULL) {
        return -1;
    }

    entry_str[0] = 0;
    (void)snprintf(entry_str, MAX_PROBE_NAME_LEN - 1, "probe_main_%s", probe->bin);
    probe->probe_entry = dlsym(hdl, (char *)entry_str);
    if (probe->probe_entry == NULL) {
        PARSE_ERR("failed to set entry for probe, unknown func: %s", entry_str);
        ret = -1;
        goto end;
    }
end:
    dlclose(hdl);
    return ret;
}

static int init_probe_bin(struct probe_s *probe, enum probe_type_e probe_type)
{
    if (probe_type >= PROBE_TYPE_MAX) {
        return -1;
    }

    probe->bin = strdup(probe_define[probe_type - 1].bin);
    if (!probe->bin) {
        return -1;
    }

    if ((probe_type != PROBE_BASEINFO) && (probe_type != PROBE_VIRT)) {
        probe->is_extend_probe = 1;
        probe->cb = extend_probe_thread_cb;
    } else {
        int ret = set_probe_entry(probe);
        if (ret) {
            return -1;
        }
        probe->is_extend_probe = 0;
        probe->cb = native_probe_thread_cb;
    }

    return 0;
}

static int get_probe_pid(struct probe_s *probe)
{
    int pid;
    (void)pthread_rwlock_wrlock(&probe->rwlock);
    pid = probe->pid;
    (void)pthread_rwlock_unlock(&probe->rwlock);
    return pid;
}

void set_probe_pid(struct probe_s *probe, int pid)
{
    (void)pthread_rwlock_wrlock(&probe->rwlock);
    probe->pid = pid;
    (void)pthread_rwlock_unlock(&probe->rwlock);
}

static char is_probe_ready(struct probe_s *probe)
{
    if (!probe->cb) {
        goto end;
    }

    if (IS_NATIVE_PROBE(probe)) {
        if (probe->probe_entry == NULL) {
            goto end;
        }
    } else {
        if (probe->bin == NULL || access(probe->bin, 0) != 0) {
            goto end;
        }

        if (probe->fifo == NULL) {
            goto end;
        }
    }

    return 1;
end:
    ERROR("[PROBEMNG] Probe is not ready(name: %s)\n", probe->name);
    return 0;
}

static int try_start_probe(struct probe_s *probe)
{
    int ret;

    if (IS_RUNNING_PROBE(probe)) {
        probe->resnd_snooper_for_restart = 0;
        return 0;
    }

    if (!IS_STARTED_PROBE(probe)) {
        probe->resnd_snooper_for_restart = 0;
        return 0;
    }

    if (!is_probe_ready(probe)) {
        return -1;
    }

    // In case that probe exited abnormally, we can clean up thread resources here
    if (probe->tid != 0) {
        pthread_join(probe->tid, NULL);
        set_probe_pid(probe, -1);
        probe->tid = 0;
    }

    (void)pthread_rwlock_wrlock(&probe->rwlock);
    ret = pthread_create(&probe->tid, NULL, probe->cb, probe);
    if (ret != 0) {
        ERROR("[PROBEMNG] Failed to create thread for probe(name: %s errno: %d).\n",
            probe->name, errno);
        (void)pthread_rwlock_unlock(&probe->rwlock);
        return -1;
    }

    probe->probe_status.status_flags |= PROBE_FLAGS_RUNNING;
    probe->probe_status.status_flags &= ~(PROBE_FLAGS_STOPPED);
    (void)pthread_rwlock_unlock(&probe->rwlock);
    probe->resnd_snooper_for_restart += 1;  // must be reset when start ends
    return 0;
}

static int start_probe(struct probe_s *probe)
{
    if (IS_RUNNING_PROBE(probe)) {
        return 0;
    }

    set_probe_status_started(probe);
    if (try_start_probe(probe)) {
        PARSE_ERR("failed to start probe");
        return -1;
    }

    return 0;
}

static int kill_extend_probe(struct probe_s *probe)
{
    int pid;
    int sig_num = SIGINT;

    pid = get_probe_pid(probe);
    if (pid < 0) {
        DEBUG("re-search probe name: %s pid when kill it before\n", probe->name);
        lkup_and_set_probe_pid(probe);
        pid = get_probe_pid(probe);
    }
    if (pid < 0) {
        PARSE_ERR("failed to find process of extend probe");
        return -1;
    }

    if (probe->probe_type == PROBE_CONTAINER ||
        probe->probe_type == PROBE_POSTGRE_SLI ||
        probe->probe_type == PROBE_SERMANT) {
        sig_num = SIGTERM;
    }

    kill(pid, sig_num);
    return 0;
}

static int stop_probe(struct probe_s *probe)
{
    if (!IS_RUNNING_PROBE(probe)) {
        return 0;
    }

    set_probe_status_stopping(probe);

    if (IS_NATIVE_PROBE(probe)) {
        set_probe_status_stopped(probe);
        clear_ipc_msg((long)(probe->probe_type == PROBE_CUSTOM) ? (PROBE_CUSTOM_IPC + probe->custom.index) : probe->probe_type);
    } else {
        if (kill_extend_probe(probe)) {
            return -1;
        }
    }
    pthread_join(probe->tid, NULL);
    probe->tid = 0;
    set_probe_pid(probe, -1);
    return 0;
}

static enum probe_type_e get_probe_type_by_name(const char *probe_name)
{
    size_t size = sizeof(probe_define) / sizeof(struct probe_define_s);

    if (probe_name == NULL) {
        PARSE_ERR("invalid probe name");
        return PROBE_TYPE_MAX;
    }

    for (size_t i = 0; i < size; i++) {
        if (!strcasecmp(probe_define[i].desc, probe_name)) {
            if (probe_define[i].enable == 0) {
                PARSE_ERR("not supported in the current version");
                return PROBE_TYPE_MAX;
            }
            return probe_define[i].type;
        }
    }

    if (g_custom_ini) {
        for (size_t i = 1; i <= g_custom_ini->custom_num; i++) {
            if (!strcmp(g_custom_ini->custom[i]->name, probe_name)) {
                return PROBE_CUSTOM;
            }
        }
    }

    PARSE_ERR("invalid probe name %s", probe_name);
    return PROBE_TYPE_MAX;
}

/* Pass the parameters of the init custom probe record to the currently probe. */
static int information_transfer(struct probe_s *custom, struct probe_s *probe)
{
    char *bin;
    unsigned int size;

    /* Transfer the bin, privilege, subprobe, params information in the JSON file. */
    bin = probe->bin;
    probe->bin = strdup(custom->bin);
    if (bin != NULL) {
        free(bin);
        bin = NULL;
    }
    if (probe->bin == NULL) {
        ERROR("[Custom]Failed to allocate memory for the bin file of probe through strdup, bin is %s.\n", custom->bin);
        return -1;
    }

    probe->custom.privilege = custom->custom.privilege;

    size = custom->custom.custom_ipc_msg.params_num;
    for (int i = 0; i < size; i++) {
        snprintf(probe->custom.custom_ipc_msg.custom_param[i].label, MAX_CUSTOM_PARAMS_LEN,
                 "%s", custom->custom.custom_ipc_msg.custom_param[i].label);
    }
    probe->custom.custom_ipc_msg.params_num = size;
    probe->custom.index = custom->custom.index;

    return 0;
}

static int find_custom_ini(struct probe_s *probe) {
    u32 num = g_custom_ini->custom_num;
    int ret;

    for (int i = 0; i <= num; i++) {
        if ((g_custom_ini->custom[i] != NULL) && (strcmp(g_custom_ini->custom[i]->name, probe->name)) == 0) {
            ret = information_transfer(g_custom_ini->custom[i], probe);
            return ret;
        }
    }
    return -1;
}

static struct probe_s *get_probe_by_name(const char *probe_name)
{
    enum probe_type_e probe_type = get_probe_type_by_name(probe_name);
    u32 custom_index = g_probe_mng->custom_index;

    if (probe_type >= PROBE_TYPE_MAX) {
        return NULL;
    }

    if (probe_type == PROBE_CUSTOM) {
        for (size_t i = 1; i <= custom_index; i++) {
            if (strcmp(probe_name, g_probe_mng->custom[i]->name) == 0) {
                return g_probe_mng->custom[i];
            }
        }
        custom_index++;
        if (custom_index >= MAX_CUSTOM_NUM) {
            PARSE_ERR("The number of external probes has over the threshold.");
            return NULL;
        }
        g_probe_mng->custom[custom_index] = new_probe(probe_name, PROBE_CUSTOM);
        if (g_probe_mng->custom[custom_index] != NULL && !find_custom_ini(g_probe_mng->custom[custom_index])) {
            g_probe_mng->custom_index = custom_index;
        }
        return g_probe_mng->custom[custom_index];
    }
    if (g_probe_mng->probes[probe_type]) {
        return g_probe_mng->probes[probe_type];
    }

    g_probe_mng->probes[probe_type] = new_probe(probe_name, probe_type);
    return g_probe_mng->probes[probe_type];
}

static void probe_printer_cmd(struct probe_s *probe, void *json)
{
    void *range;
    Json_AddStringToObject(json, "bin", probe->bin ? :"");
    size_t size = 0;

    range = Json_CreateArray();
    if (probe->probe_type == PROBE_CUSTOM) {
        size = probe->custom.custom_ipc_msg.subprobe_num;

        for (size_t i = 0; i < size; i++) {
            if (probe->custom.custom_ipc_msg.subprobe[i] != NULL) {
                Json_AddStringItemToArray(range, probe->custom.custom_ipc_msg.subprobe[i]);
            }
        }
    } else {
        size = sizeof(probe_range_define) / sizeof(struct probe_range_define_s);

        for (size_t i = 0; i < size; i++) {
            if (probe->probe_type == probe_range_define[i].probe_type) {
                if (probe->probe_range_flags & probe_range_define[i].flags) {
                    Json_AddStringItemToArray(range, probe_range_define[i].desc);
                }
            }
        }
    }
    Json_AddItemToObject(json, "probe", range);
    Json_Delete(range);
}

static int custom_subprobe_parser(struct probe_s *probe, const char *subprobe)
{
    unsigned int subsize;
    size_t index;

    if (subprobe == NULL) {
        return -1;
    }

    subsize = g_custom_ini->custom[probe->custom.index]->custom.custom_ipc_msg.subprobe_num;
    for (index = 0; index < subsize; index++) {
        if (strcmp(subprobe, g_custom_ini->custom[probe->custom.index]->custom.custom_ipc_msg.subprobe[index]) == 0) {
            break;
        }
    }
    if (index == subsize) {
        PARSE_ERR("invalid external probe range: subprobe name error,name is %s.", subprobe);
        return -1;
    }
    snprintf(probe->custom.custom_ipc_msg.subprobe[probe->custom.custom_ipc_msg.subprobe_num],
             MAX_CUSTOM_NAME_LEN, "%s", subprobe);
    (probe->custom.custom_ipc_msg.subprobe_num)++;
    return 0;
}
/* {"probe":["XX","YY"]} , XX must be string and must be in supported probe range*/
static int probe_parser_range(struct probe_s *probe, void *probe_item)
{
    int range = 0;
    void *object;
    unsigned int subnum_bak = 0;
    char subprobe_bak[MAX_SUBPROBE_NUM][MAX_CUSTOM_NAME_LEN] = {0};
    int ret = 0;

    if (probe->probe_type == PROBE_CUSTOM) {
        for (int j = 0; j < probe->custom.custom_ipc_msg.subprobe_num; j++) {
            snprintf(subprobe_bak[j], MAX_CUSTOM_NAME_LEN, "%s", probe->custom.custom_ipc_msg.subprobe[j]);
        }
        subnum_bak = probe->custom.custom_ipc_msg.subprobe_num;
        memset(probe->custom.custom_ipc_msg.subprobe, 0, sizeof(char) * MAX_SUBPROBE_NUM * MAX_CUSTOM_NAME_LEN);
        probe->custom.custom_ipc_msg.subprobe_num = 0;
    }
    probe->probe_range_flags = 0;
    size_t size = Json_GetArraySize(probe_item);
    for (size_t i = 0; i < size; i++) {
        object = Json_GetArrayItem(probe_item, i);
        if (!Json_IsString(object)) {
            PARSE_ERR("Invalid probe range: must be string.");
            return -1;
        }
        if (check_custom_range((char *)Json_GetValueString(object), "name")) {
            PARSE_ERR("The lengh of probe name must range from 0 to %d.", MAX_CUSTOM_NAME_LEN);
            return -1;
        }

        if (probe->probe_type == PROBE_CUSTOM) {
            ret = custom_subprobe_parser(probe, (const char*)Json_GetValueString(object));
            if (ret) {
                for (int j = 0; j < subnum_bak; j++) {
                    memset(probe->custom.custom_ipc_msg.subprobe[j], 0, sizeof(char) * MAX_CUSTOM_NAME_LEN);
                    snprintf(probe->custom.custom_ipc_msg.subprobe[j], MAX_CUSTOM_NAME_LEN, "%s", subprobe_bak[j]);
                }
                probe->custom.custom_ipc_msg.subprobe_num = subnum_bak;
                return -1;
            }
        } else {
            range = get_probe_range(probe->probe_type, (const char*)Json_GetValueString(object));
            if (!range) {
                PARSE_ERR("unsupported probe range: %s.", (const char*)Json_GetValueString(object));
                return -1;
            }
        }
        probe->probe_range_flags |= (u32)range;
    }

    return check_probe_range(probe);
}

char *probe_extern_cmd_param(const char *key, const void *item)
{
    void *probe_object;

    probe_object = Json_GetObjectItem(item, key);
    if (probe_object == NULL || !Json_IsString(probe_object)) {
        PARSE_ERR("invalid probe range: must be string and not null.");
        return NULL;
    }
    return (char *)Json_GetValueString(probe_object);
}

static int custom_params_ini(struct probe_s *probe, char *param) {
    char *ptr = NULL;
    int count = 0;

    if (param == NULL) {
        return -1;
    }
    ptr = strtok(param, ",");
    while (ptr != NULL) {
        if (count == MAX_CUSTOM_PARAMS_NUM) {
            ERROR("[CUSTOM INI]The number of custom params must less than MAX_CUSTOM_PARAMS_NUM.\n");
            return -1;
        }
        if (check_custom_range(ptr, "param")) {
            ERROR("[CUSTOM INI]The length of custom params must range from 0 to %d.\n", MAX_CUSTOM_PARAMS_LEN);
            return -1;
        }
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(ptr, probe->custom.custom_ipc_msg.custom_param[i].label) == 0) {
                ERROR("[CUSTOM INI] Duplicate parameter names are not allowed.");
                return -1;
            }
        }
        snprintf(probe->custom.custom_ipc_msg.custom_param[count].label, MAX_CUSTOM_PARAMS_LEN, "%s", ptr);
        INFO("The NO.%d param is %s.\n", count, ptr);
        count++;
        ptr = strtok(NULL, ",");
    }
    probe->custom.custom_ipc_msg.params_num = count;
    return 0;
}

static int probe_parser_cmd(struct probe_s *probe, const void *item)
{
    int ret = 0;
    void *probe_object;
    char *bin;

    probe_object = Json_GetObjectItem(item, "probe");
    if (probe_object == NULL) {
        ERROR("[Custom ini]:Faild get probe params from json.\n");
        return -1;
    }
    ret = probe_parser_range(probe, probe_object);
    if (ret) {
        return ret;
    }

    /* Custom probe dedicated, there are tow scenarios.*/
    if (probe->probe_type == PROBE_CUSTOM) {
        bin = probe_extern_cmd_param("bin", item);
        if (bin == NULL || strcmp(probe->bin, bin)) {
            PARSE_ERR("invalid custom probe bin path");
            return -1;
        }
    }

    return ret;
}

static int custom_parser(struct probe_s *probe, const void *item)
{
    int ret = 0;
    size_t size;
    void *probe_object;
    void *object;
    char *type;

    probe_object = Json_GetObjectItem(item, "probe");
    if (probe_object == NULL) {
        ERROR("[Custom ini]Invalid json format: please check probe value.\n");
        return -1;
    }

    size = Json_GetArraySize(probe_object);
    if (size > MAX_SUBPROBE_NUM) {
        ERROR("[Custom ini]The number of subprobe must less than %d.\n", MAX_SUBPROBE_NUM);
        return -1;
    }

    for (size_t i = 0; i < size; i++) {
        object = Json_GetArrayItem(probe_object, i);
        if (!Json_IsString(object)) {
            ERROR("[Custom ini]Invalid probe range: must be string.\n");
            return -1;
        }
        if (check_custom_range((char *)Json_GetValueString(object), "name")) {
            ERROR("[Custom ini]The lengh of probe name must range from 0 to %d.\n", MAX_CUSTOM_NAME_LEN);
            return -1;
        }

        snprintf(probe->custom.custom_ipc_msg.subprobe[i], MAX_CUSTOM_NAME_LEN, "%s", (char *)Json_GetValueString(object));
        (probe->custom.custom_ipc_msg.subprobe_num)++;
    }

    ret = check_duplicate(probe->custom.custom_ipc_msg.subprobe, probe->custom.custom_ipc_msg.subprobe_num);
    if (ret) {
        ERROR("[Custom ini] Duplicate subprobe names are not allowed.\n");
        return -1;
    }

    type = probe_extern_cmd_param("bin", item);
    if (type == NULL) {
        ERROR("[Custom ini] Invalid json format: please check bin value.\n");
        return -1;
    }

    if (check_path_for_security(type)) {
        ERROR("[Custom ini] Custom probe bin contains insecure characters.\n");
        return -1;
    }

    probe->bin = strdup(type);
    if (probe->bin == NULL) {
        ERROR("[Custom ini] Failed to strdup custom probe bin\n");
        return -1;
    }

    if (check_custom_range(probe->bin, "bin")) {
        ERROR("[Custom ini]:The length of bin must range from 0 to %d.\n", MAX_BIN_LEN);
        return -1;
    }

    probe->custom.privilege = false;

    type = probe_extern_cmd_param("custom_param", item);
    if (type == NULL) {
        ERROR("[Custom ini]Invalid json format: please check custom_param value.\n");
        return -1;
    }
    ret = custom_params_ini(probe, type);
    if (ret) {
        ERROR("[Custom ini]invalid json type, check custom params.\n");
        return -1;
    }

    return 0;
}

static void probe_backup_cmd(struct probe_s *probe, struct probe_s *probe_backup)
{
    probe_backup->is_extend_probe = probe->is_extend_probe;
    probe_backup->probe_entry = probe->probe_entry;
    probe_backup->cb = probe->cb;
    probe_backup->probe_range_flags = probe->probe_range_flags;
}

static void probe_rollback_cmd(struct probe_s *probe, struct probe_s *probe_backup)
{
    probe->is_extend_probe = probe_backup->is_extend_probe;
    probe->probe_entry = probe_backup->probe_entry;
    probe->cb = probe_backup->cb;

    probe->probe_range_flags = probe_backup->probe_range_flags;
}


static int probe_parser_state(struct probe_s *probe, const void *item)
{
    if (!Json_IsString(item)) {
        PARSE_ERR("invalid state: must be string");
        return -1;
    }

    if (!strcasecmp(PROBE_STATE_RUNNING, (const char *)Json_GetValueString(item))) {
        if (check_probe_range(probe) || check_probe_snooper_conf_num(probe)) {
            return -1;
        }
        return start_probe(probe);
    }

    if (!strcasecmp(PROBE_STATE_STOPPED, (const char *)Json_GetValueString(item))) {
        return stop_probe(probe);
    }

    PARSE_ERR("invalid state %s: must be running or stopped", (const char *)Json_GetValueString(item));
    return -1;
}

static void print_state(struct probe_s *probe, void *json)
{
    char *state;

    if (probe->probe_status.status_flags == 0) {
        state = PROBE_STATE_UNKNOWN;
    } else if (IS_STOPPED_PROBE(probe)) {
        state = PROBE_STATE_STOPPED;
    } else {
        state = PROBE_STATE_RUNNING;
    }
    Json_AddStringToObject(json, "state", state);
}

static int probe_parser_params(struct probe_s *probe, const void *item)
{
    return parse_params(probe, item);
}

static void print_params(struct probe_s *probe, void *json)
{
    probe_params_to_json(probe, json);
}

static void custom_param_backup(struct custom *custom, struct custom *custom_backup)
{
    custom_backup->index = custom->index;
    custom_backup->privilege = custom->privilege;
    custom_backup->custom_ipc_msg.params_num = custom->custom_ipc_msg.params_num;
    custom_backup->custom_ipc_msg.subprobe_num = custom->custom_ipc_msg.subprobe_num;
    for (int i = 0; i < custom->custom_ipc_msg.subprobe_num; i++) {
        snprintf(custom_backup->custom_ipc_msg.subprobe[i], MAX_CUSTOM_NAME_LEN, "%s", custom->custom_ipc_msg.subprobe[i]);
    }
    for (int i = 0; i < custom->custom_ipc_msg.params_num; i++) {
        snprintf(custom_backup->custom_ipc_msg.custom_param[i].label, MAX_CUSTOM_PARAMS_LEN, "%s", custom->custom_ipc_msg.custom_param[i].label);
        snprintf(custom_backup->custom_ipc_msg.custom_param[i].value, MAX_CUSTOM_PARAMS_LEN, "%s", custom->custom_ipc_msg.custom_param[i].value);
    }
}

static void custom_param_rollback(struct custom *custom, struct custom *custom_backup)
{
    custom->index = custom_backup->index;
    custom->privilege = custom_backup->privilege;
    custom->custom_ipc_msg.params_num = custom_backup->custom_ipc_msg.params_num;
    custom->custom_ipc_msg.subprobe_num = custom_backup->custom_ipc_msg.subprobe_num;
    for (int i = 0; i < custom->custom_ipc_msg.subprobe_num; i++) {
        snprintf(custom->custom_ipc_msg.subprobe[i], MAX_CUSTOM_NAME_LEN, "%s", custom_backup->custom_ipc_msg.subprobe[i]);
    }
    for (int i = 0; i < custom->custom_ipc_msg.params_num; i++) {
        snprintf(custom->custom_ipc_msg.custom_param[i].label, MAX_CUSTOM_PARAMS_LEN, "%s", custom_backup->custom_ipc_msg.custom_param[i].label);
        snprintf(custom->custom_ipc_msg.custom_param[i].value, MAX_CUSTOM_PARAMS_LEN, "%s", custom_backup->custom_ipc_msg.custom_param[i].value);
    }
}

static void probe_backup_params(struct probe_s *probe, struct probe_s *probe_backup)
{
    memcpy(&probe_backup->probe_param, &probe->probe_param, sizeof(struct probe_params));
    if (probe->probe_type == PROBE_CUSTOM) {
        custom_param_backup(&probe->custom, &probe_backup->custom);
    }
}

static void probe_rollback_params(struct probe_s *probe, struct probe_s *probe_backup)
{
    memcpy(&probe->probe_param, &probe_backup->probe_param, sizeof(struct probe_params));
    if (probe->probe_type == PROBE_CUSTOM) {
        custom_param_rollback(&probe->custom, &probe_backup->custom);
    }
}

typedef int (*probe_json_parser)(struct probe_s *, const void *);
typedef void (*probe_json_printer)(struct probe_s *, void *);
typedef void (*probe_backuper)(struct probe_s *, struct probe_s *);
typedef void (*probe_rollbacker)(struct probe_s *, struct probe_s *);
struct probe_parser_s {
    const char *item;
    probe_json_parser parser;
    probe_json_printer printer;
    probe_backuper backuper;
    probe_rollbacker rollbacker;
};

// !!!NOTICE:The function sequence and macros cannot be changed.
#define PARSER_FLAG_CMD       0x01
#define PARSER_FLAG_SNOOPERS  0x02
#define PARSER_FLAG_PARAMS    0x04
#define PARSER_FLAG_STATE     0x08
struct probe_parser_s probe_parsers[] = {
    {"cmd",      probe_parser_cmd,    probe_printer_cmd, probe_backup_cmd,    probe_rollback_cmd},
    {"snoopers", parse_snooper,       print_snooper,     backup_snooper,      rollback_snooper},
    {"params",   probe_parser_params, print_params,      probe_backup_params, probe_rollback_params},
    {"state",    probe_parser_state,  print_state,       NULL,                NULL}
};

static void rollback_probe(struct probe_s *probe, struct probe_s *probe_backup, u32 flag)
{
    struct probe_parser_s *parser;

    if (!probe || !probe_backup) {
        return;
    }

    size_t size = sizeof(probe_parsers) / sizeof(struct probe_parser_s);
    for (size_t i = 0; i < size; i++) {
        if ((flag >> i) & 0x1) {
            parser = &(probe_parsers[i]);

            if (parser->rollbacker) {
                parser->rollbacker(probe, probe_backup);
            }
        }
    }
}

#define __COMP_STR_P(a, b, is_modify) \
    do \
    {\
        is_modify = 0; \
        if (((a) != NULL) && ((b) != NULL)) { \
            if (strcmp((a), (b))) { \
                is_modify = 1; \
            } \
        } \
        \
        if ((((a) != NULL) && ((b) == NULL)) || (((b) != NULL) && ((a) == NULL))) { \
            is_modify = 1; \
        } \
    } while (0)

static char __snooper_app_is_modify(struct snooper_conf_s* conf, struct snooper_conf_s *backup_conf)
{
    char is_modify = 0;

    if (strcmp(conf->conf.app.comm, backup_conf->conf.app.comm)) {
        return 1;
    }

    __COMP_STR_P(conf->conf.app.cmdline, backup_conf->conf.app.cmdline, is_modify);
    if (is_modify) {
        return 1;
    }

    __COMP_STR_P(conf->conf.app.debuging_dir, backup_conf->conf.app.debuging_dir, is_modify);
    if (is_modify) {
        return 1;
    }

    return 0;

}

static char __snooper_proc_is_modify(struct snooper_conf_s* conf, struct snooper_conf_s *backup_conf)
{
    if (conf->conf.proc_id != backup_conf->conf.proc_id) {
        return 1;
    }
    return 0;
}
static char __snooper_pod_is_modify(struct snooper_conf_s* conf, struct snooper_conf_s *backup_conf)
{
    if (strcmp(conf->conf.pod_id, backup_conf->conf.pod_id)) {
        return 1;
    }
    return 0;
}
static char __snooper_container_is_modify(struct snooper_conf_s* conf, struct snooper_conf_s *backup_conf)
{
    if (strcmp(conf->conf.container_id, backup_conf->conf.container_id)) {
        return 1;
    }
    return 0;
}

static char __snooper_container_name_is_modify(struct snooper_conf_s* conf, struct snooper_conf_s *backup_conf)
{
    if (strcmp(conf->conf.container_name, backup_conf->conf.container_name)) {
        return 1;
    }
    return 0;
}

typedef char (*snooper_is_modify_cb)(struct snooper_conf_s *, struct snooper_conf_s *);
struct snooper_modify_s {
    enum snooper_conf_e type;
    snooper_is_modify_cb is_modify;
};

struct snooper_modify_s snooper_modifys[] = {
    {SNOOPER_CONF_APP, __snooper_app_is_modify},
    {SNOOPER_CONF_PROC_ID, __snooper_proc_is_modify},
    {SNOOPER_CONF_POD_ID, __snooper_pod_is_modify},
    {SNOOPER_CONF_CONTAINER_ID, __snooper_container_is_modify},
    {SNOOPER_CONF_CONTAINER_NAME, __snooper_container_name_is_modify}
};


static char snooper_is_modify(struct snooper_conf_s* conf, struct snooper_conf_s *backup_conf)
{
    if (conf->type != backup_conf->type) {
        return 1;
    }

    if (snooper_modifys[conf->type].is_modify == NULL) {
        return 1;
    }

    return snooper_modifys[conf->type].is_modify(conf, backup_conf);
}

static void set_probe_modify(struct probe_s *probe, struct probe_s *backup_probe, u32 parse_flag)
{
    char is_modify;

    // reset
    probe->is_params_chg = 0;
    probe->is_snooper_chg = 0;

    if ((parse_flag & PARSER_FLAG_CMD) &&
        (probe->probe_range_flags != backup_probe->probe_range_flags)) {
        probe->is_params_chg = 1;
    }

    if ((parse_flag & PARSER_FLAG_PARAMS) &&
        (memcmp(&(probe->probe_param), &(backup_probe->probe_param), sizeof(struct probe_params)))) {
        probe->is_params_chg = 1;
    }

    if (!(parse_flag & PARSER_FLAG_SNOOPERS)) {
        return;
    }

    if (probe->snooper_conf_num != backup_probe->snooper_conf_num) {
        probe->is_snooper_chg = 1;
        return;
    }

    is_modify = 0;
    for (int i = 0; i < probe->snooper_conf_num && i < SNOOPER_CONF_MAX; i++) {
        if ((probe->snooper_confs[i] == NULL) || (backup_probe->snooper_confs[i] == NULL)) {
            is_modify = 1;
            break;
        }
        if (snooper_is_modify(probe->snooper_confs[i], backup_probe->snooper_confs[i])) {
            is_modify = 1;
            break;
        }
    }
    if (is_modify) {
        probe->is_snooper_chg = 1;
    }
    return;
}

static void probe_info_print(struct probe_s *probe)
{
    INFO("probe name = %s\n", probe->name);
    INFO("probe bin = %s\n", probe->bin);
    INFO("probe privilege = %s\n", (probe->custom.privilege == true ? "true" : "false"));
    for (int i = 0; i < probe->custom.custom_ipc_msg.subprobe_num; i++) {
        INFO("probe subprobe %d is %s\n", i, probe->custom.custom_ipc_msg.subprobe[i]);
    }
}

int parse_probe_json(const char *probe_name, const char *probe_content)
{
    int ret = -1;
    u32 parse_flag = 0;
    struct probe_parser_s *parser;
    struct probe_s *probe_backup = NULL;

    void *jsonObj = NULL;
    void *itemObj = NULL;

    get_probemng_lock();

    PARSE_ERR("Bad request");
    struct probe_s *probe = get_probe_by_name(probe_name);
    if (probe == NULL) {
        goto end;
    }
    jsonObj = Json_Parse(probe_content);

    if (jsonObj == NULL) {
        PARSE_ERR("invalid json format");
        goto end;
    }

    probe_backup = (struct probe_s *)calloc(1, sizeof(struct probe_s));
    if (probe_backup == NULL) {
        goto end;
    }

    size_t size = sizeof(probe_parsers) / sizeof(struct probe_parser_s);
    for (size_t i = 0; i < size; i++) {
        parser = &(probe_parsers[i]);

        itemObj = Json_GetObjectItem(jsonObj, parser->item);

        if (itemObj == NULL) {
            continue;
        }

        parse_flag |= 0x1 << i;
        if (parser->backuper) {
            parser->backuper(probe, probe_backup);
        }
        ret = parser->parser(probe, itemObj);
        if (ret) {
            rollback_probe(probe, probe_backup, parse_flag);
            break;
        }
    }

    if (ret == 0) {
        set_probe_modify(probe, probe_backup, parse_flag);
    }

    if (ret == 0) {
        probe_info_print(probe);
    }

    /* Send snooper obj after parsing successfully */
    if (ret == 0 && (IS_STARTED_PROBE(probe) || IS_RUNNING_PROBE(probe)) &&
        (probe->is_params_chg || probe->is_snooper_chg || probe->resnd_snooper_for_restart)) {
        ret = send_snooper_obj(probe);
        if (ret) {
            PARSE_ERR("failed to send ipc msg to probe");
        }
    }

    probe->resnd_snooper_for_restart = 0;
    destroy_probe(probe_backup);
end:
    put_probemng_lock();
    if (jsonObj) {
        Json_Delete(jsonObj);
    }
    return ret;
}

char *get_probe_json(const char *probe_name)
{
    void *res = NULL, *item;
    char *buf = NULL;
    struct probe_s *probe = NULL;
    struct probe_parser_s *parser;

    get_probemng_lock();

    enum probe_type_e probe_type = get_probe_type_by_name(probe_name);
    if (probe_type >= PROBE_TYPE_MAX) {
        goto end;
    }

    res = Json_CreateObject();
    if (probe_type == PROBE_CUSTOM) {
        for (int i = 1; i <= g_probe_mng->custom_index; i++) {
            if (strcmp(g_probe_mng->custom[i]->name, probe_name) == 0) {
                probe = g_probe_mng->custom[i];
                break;
            }
            if (i == g_probe_mng->custom_index) {
                goto end;
            }
        }
    } else {
        probe = g_probe_mng->probes[probe_type];
    }
    if (probe == NULL) {
        goto end;
    }

    size_t size = sizeof(probe_parsers) / sizeof(struct probe_parser_s);
    for (size_t i = 0; i < size; i++) {
        parser = &(probe_parsers[i]);
        if (parser->printer) {
            if (strcmp(parser->item, "state") == 0) {
                parser->printer(probe, res);
            } else {
                item = Json_CreateObject();
                parser->printer(probe, item);
                Json_AddItemToObject(res, parser->item, item);
                Json_Delete(item);
            }
        }
    }

end:
    if (res) {
        buf = Json_PrintUnformatted(res);
        Json_Delete(res);
    }
    put_probemng_lock();
    return buf;
}

void destroy_probe_threads(void)
{
    if (g_probe_mng == NULL) {
        return;
    }

    for (int i = 0; i < PROBE_TYPE_MAX; i++) {
        struct probe_s *probe = g_probe_mng->probes[i];
        if (probe != NULL) {
            stop_probe(probe);
            INFO("[PROBE_MNG] Probe %s is stopped.\n", probe->name);
        }
    }
}

void destroy_probe_mng(void)
{
    if (g_probe_mng == NULL) {
        return;
    }

    destroy_ipc_msg_queue(g_probe_mng->msq_id);
    g_probe_mng->msq_id = -1;

    (void)pthread_rwlock_destroy(&g_probe_mng->rwlock);
    destroy_probe_threads();
    for (int i = 0; i < PROBE_TYPE_MAX; i++) {
        destroy_probe(g_probe_mng->probes[i]);
        g_probe_mng->probes[i] = NULL;
    }

    unload_snooper_bpf(g_probe_mng);
    free(g_probe_mng);
    g_probe_mng = NULL;
    del_pods();
}

void destroy_custom_ini(void)
{
    struct probe_s *probe;

    if (g_custom_ini == NULL) {
        return;
    }
    for (int i = 0; i <= g_custom_ini->custom_num; i++) {
        probe = g_custom_ini->custom[i];
        if (probe == NULL) {
            continue;
        }
        stop_probe(probe);
        INFO("[CUSTOM INI]Probe %s is stopped\n", probe->name);
        destroy_probe(g_custom_ini->custom[i]);
        g_custom_ini->custom[i] = NULL;
    }
    free(g_custom_ini);
    g_custom_ini = NULL;
}

static struct probe_s* new_custom(const char* name, const u32 custom_index)
{
    struct probe_s *probe = NULL;
    size_t size = sizeof(probe_define) / sizeof(struct probe_define_s);

    for (int i = 0; i < size; i++) {
        if (strcmp(name, probe_define[i].desc) == 0) {
            ERROR("[CUSTOM INI]Custom name cannot be the same as internal probe.");
            return NULL;
        }
    }

    probe = (struct probe_s *)calloc(1, sizeof(struct probe_s));
    if (probe == NULL) {
        ERROR("[CUSTOM INI]Probe memory allocation failure.\n");
        return NULL;
    }

    probe->name = strdup(name);
    if (probe->name == NULL) {
        ERROR("[CUSTOM INI]Probe name memory strdup failure.\n");
        free(probe);
        return NULL;
    }

    probe->probe_type = PROBE_CUSTOM;
    probe->custom.index = custom_index;
    probe->bin = NULL;
    set_default_params(probe);

    return probe;
}

static struct probe_s *creare_custom_by_name(char *name)
{
    u32 custom_index = g_custom_ini->custom_num;

    if (check_custom_range(name, "name")) {
        ERROR("[CUSTOM INIT]The length of custom name has out of range, it must range from 0 to %d.\n", MAX_CUSTOM_NAME_LEN);
        return NULL;
    }
    custom_index++;
    if (custom_index >= MAX_CUSTOM_NUM) {
        ERROR("[CUSTOM INIT]The number of custom probes has over the threshold.\n");
        return NULL;
    }
    g_custom_ini->custom[custom_index] = new_custom(name, custom_index);
    if (g_custom_ini->custom[custom_index] != NULL) {
        g_custom_ini->custom_num = custom_index;
    }
    return g_custom_ini->custom[custom_index];
}
/* Read data from a file*/
int file_handle(const char *file, char *data, size_t size)
{
    FILE *fp;
    u32 fsize;
    size_t file_size;

    fp = fopen(file, "r");
    if (fp == NULL) {
        ERROR("[CUSTOM INI]Unable to open file.\n");
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    file_size = fread(data, 1, fsize, fp);
    if (file_size != fsize || file_size >= size) {
        ERROR("[CUSTOM INI]Please verify the value of file_size.\n");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    data[fsize] = '\0';
    return 0;
}

static int init_custom_probe(const char* data)
{
    void *json_obj = NULL;
    struct key_value_pairs *kv_pairs = NULL;
    struct key_value *kv;
    struct probe_s *probe;

    json_obj = Json_Parse(data);
    if (json_obj == NULL) {
        ERROR("[CUSTOM INI]invalid json format.\n");
        return -1;
    }

    kv_pairs = Json_GetKeyValuePairs(json_obj);
    if (!kv_pairs) {
        ERROR("[CUSTOM INI]invalid json param pairs.\n");
        Json_Delete(json_obj);
        return -1;
    }
    Json_ArrayForEach(kv, kv_pairs) {
        probe = creare_custom_by_name(kv->key);
        if (probe == NULL) {
            ERROR("[CUSTOM INI]Failed to initialize the custom with name.\n");
            Json_DeleteKeyValuePairs(kv_pairs);
            Json_Delete(json_obj);
            return -1;
        }

        if (custom_parser(probe, kv->valuePtr)) {
            Json_DeleteKeyValuePairs(kv_pairs);
            Json_Delete(json_obj);
            return -1;
        }
    }

    Json_DeleteKeyValuePairs(kv_pairs);
    Json_Delete(json_obj);
    for(int i = 1; i <= g_custom_ini->custom_num; ++i) {
        INFO("[CUSTOM INI] custom probe %s has been obtained successfully.\n", g_custom_ini->custom[i]->name);
    }
    return 0;
}

static int init_custom(const char *custom_path)
{
    char *json_data;
    int ret;

    json_data = calloc(1, MAX_CUSTOM_CONFIG + 1);
    if (json_data == NULL) {
        ERROR("[CUSTOM INI]Custom memory allocation error\n");
        return -1;
    }
    ret = file_handle(custom_path, json_data, MAX_CUSTOM_CONFIG + 1);
    if (ret) {
        free(json_data);
        return ret;
    }

    ret = init_custom_probe(json_data);
    free(json_data);
    if (ret) {
        return ret;
    }

    return 0;
}

static int verify_single_permission(char *path, bool is_true, unsigned int user, unsigned int group, unsigned int other)
{
    struct stat st;
    struct passwd *pw;

    if (lstat(path, &st)) {
        ERROR("%s stat has error, Check whether the file exists.\n", path);
        return -1;
    }

    if ((st.st_mode & S_IFMT) == S_IFLNK) {
        ERROR("%s is a symbolic link\n", path);
        return -1;
    }

    if ((st.st_mode & S_IFMT) != S_IFREG) {
        ERROR("The path %s is not a standard path.\n", path);
        return -1;
    }

    if (is_true == true) {
        if (st.st_uid != 0) {
            ERROR("%s must owned by root.\n", path);
            return -1;
        }
    } else {
        pw = getpwuid(st.st_uid);
        if (pw == NULL) {
            ERROR("Failed to obtain the owner name. Check the owner.\n");
            return -1;
        }

        if (strcmp(pw->pw_name, "gala-gopher") != 0) {
            ERROR("Failed to verify %s, the owner is not gala-gopher.\n", path);
            return -1;
        }
    }

    if (((st.st_mode & S_IRWXU) == user) && ((st.st_mode & S_IRWXG) == group) && ((st.st_mode & S_IRWXO) == other)) {
        return 0;
    }
    ERROR("%s has permission error.\n", path);
    return -1;

}

static int verify_permissions()
{
    int ret = 0;
    int size = g_custom_ini->custom_num;

    for (int i = 1; i <= size; i++) {
        ret = verify_single_permission(g_custom_ini->custom[i]->bin, true, (S_IRUSR | S_IXUSR), (S_IRGRP | S_IXGRP), 0);
        if (ret) {
            return -1;
        }
    }
    ret = verify_single_permission(GALA_GOPHER_CUSTOM_PATH, true, (S_IRUSR | S_IWUSR), S_IRGRP, 0);
    if (ret) {
        return -1;
    }
    return 0;
}

int is_file_exist(char *dir)
{
    FILE *file = fopen(dir, "r");

    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        if (ftell(file) != 0) {
            (void)fclose(file);
            return 0;
        }
        INFO("The custom probe json is null, gala-gopher continues.\n");
        (void)fclose(file);
        return -1;
    }
    INFO("The custom probe json does not exist, gala-gopher continues.\n");
    return -1;
}

struct probe_mng_s *create_probe_mng(void)
{
    int msq_id;

    if (g_probe_mng != NULL) {
        return g_probe_mng;
    }

    g_probe_mng = (struct probe_mng_s *)malloc(sizeof(struct probe_mng_s));
    if (g_probe_mng == NULL) {
        return NULL;
    }

    memset(g_probe_mng, 0, sizeof(struct probe_mng_s));
    g_probe_mng->ingress_epoll_fd = -1;
    g_probe_mng->msq_id = -1;

    int ret = pthread_rwlock_init(&g_probe_mng->rwlock, NULL);
    if (ret) {
        goto err;
    }

    ret = load_snooper_bpf(g_probe_mng);
    if (ret) {
        goto err;
    }

    msq_id = create_ipc_msg_queue(IPC_CREAT | IPC_EXCL);
    if (msq_id < 0) {
        goto err;
    }
    g_probe_mng->msq_id = msq_id;

    g_probe_mng->keeplive_ts = (time_t)time(NULL);

    if (is_file_exist(GALA_GOPHER_CUSTOM_PATH)) {
        return g_probe_mng;
    }

    g_custom_ini = (struct custom_ini *)calloc(1, sizeof(struct custom_ini));
    if (g_custom_ini == NULL) {
        goto err;
    }
    ret = init_custom(GALA_GOPHER_CUSTOM_PATH);
    if (ret) {
        goto err;
    }

    ret = verify_permissions();
    if (ret) {
        goto err;
    }
    return g_probe_mng;

err:
    destroy_probe_mng();
    destroy_custom_ini();
    return NULL;
}

#define __PROBE_KEEPLIVE_TIMEOUT    (60) // 60 Seconds
static int is_start_today(const struct probe_mng_s *probe_mng, u8 *restart)
{
    time_t time = probe_mng->keeplive_ts;
    time_t time2 = probe_mng->keeplive_ts - __PROBE_KEEPLIVE_TIMEOUT * 2;
    struct tm tm_now, tm_before;

    localtime_r(&time, &tm_now);
    localtime_r(&time2, &tm_before);


    if (*restart < MAX_RESTART_TIMES) {
        return 1;
    }

    if (tm_now.tm_mday == tm_before.tm_mday) {
        return 0;
    }
    INFO("Time comes the next day, reset the restart times.\n");
    *restart = 0;
    return 1;
}

static void keeplive_probes(struct probe_mng_s *probe_mng)
{
    struct probe_s *probe;

    for (int i = 0; i < PROBE_TYPE_MAX; i++) {
        probe = probe_mng->probes[i];
        if (probe == NULL) {
            continue;
        }

        if (try_start_probe(probe) == 0 && probe->resnd_snooper_for_restart) {
            probe->is_params_chg = 0;
            probe->is_snooper_chg = 0;
            (void)send_snooper_obj(probe);
            probe->resnd_snooper_for_restart = 0;
        }
    }

    for (int i = 1; i <= probe_mng->custom_index; i++) {
        probe = probe_mng->custom[i];
        if (probe == NULL) {
            continue;
        }

        if (is_start_today(probe_mng, &probe->resnd_snooper_for_restart)) {
            if(try_start_probe(probe) == 0 && (probe->resnd_snooper_for_restart > 0)) {
                probe->is_params_chg = 0;
                probe->is_snooper_chg = 0;
                (void)send_snooper_obj(probe);
                INFO("probe %s has been tested to start.\n", probe->name);
            }
        }
    }
}

static char is_keeplive_tmout(struct probe_mng_s *probe_mng)
{
    time_t current = (time_t)time(NULL);
    time_t secs;

    if (current > probe_mng->keeplive_ts) {
        secs = current - probe_mng->keeplive_ts;
        if (secs >= __PROBE_KEEPLIVE_TIMEOUT) {
            probe_mng->keeplive_ts = current;
            return 1;
        }
    }
    return 0;
}

#define SNOOPER_POLL_TMOUT  100  // 100ms
void run_probe_mng_daemon(struct probe_mng_s *probe_mng)
{
    int ret;

    for (;;) {
        if (probe_mng->snooper_proc_pb != NULL) {
            ret = bpf_buffer__poll((struct bpf_buffer *)probe_mng->snooper_proc_pb, SNOOPER_POLL_TMOUT);
            if (ret < 0 && ret != -EINTR) {
                break;
            }
        }

        if (probe_mng->snooper_cgrp_pb != NULL) {
            ret = bpf_buffer__poll((struct bpf_buffer *)probe_mng->snooper_cgrp_pb, SNOOPER_POLL_TMOUT);
            if (ret < 0 && ret != -EINTR) {
                break;
            }
        }

        if (is_keeplive_tmout(probe_mng)) {
            get_probemng_lock();
            keeplive_probes(probe_mng);
            put_probemng_lock();
        }
    }
}


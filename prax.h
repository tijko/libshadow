#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#ifdef __x86_64__
#define SETIOPRIO 251
#define GETIOPRIO 252
#define TGKILL 234
#elif __i386__
#define SETIOPRIO 289
#define GETIOPRIO 290
#define TGKILL 270
#endif

#define IOPRIO_SIZE 6 
#define STATUS_SIZE 1024
#define MAX_FIELD 32
#define LINE_SZ 256
#define MAX_MSG 1024

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <linux/netlink.h>
#include <linux/taskstats.h>
#include <linux/genetlink.h>

const char *class[4] = {"", "rt/", "be/", "idle"};

// Typedef to contain stats of file-descriptors.
typedef struct fdstats fdstats_t;

struct fdstats {
    char *file;
    struct stat file_stats;
    fdstats_t *next_fd;
};

struct taskmsg {
    struct nlmsghdr nl;
    struct genlmsghdr gnl;
    char buffer[MAX_MSG];
};

// Typedef of the process being profiled.
typedef struct profile profile_t;

/*
 * Uptime - add a field to show the process' creation time
 * and total uptime at point of call.
 *
 */

#define PROCFS_MAX 32

struct profile {
    uint32_t start_time;
    uint64_t vol_ctxt_swt;
    uint64_t invol_ctxt_swt;
    uint64_t vmem;
    char procfs_base[PROCFS_MAX + 1];
    size_t procfs_len;
    char *name;
    char *username;
    char ioprio[16];
    struct proc_rlim *prlim;
    struct proc_signal *psig;
    fdstats_t *fd;
    pid_t trace_pid;
    pid_t pid;
    uid_t uid;
    int tgid;
    // a max here?
    int ctty;
    // relatively low
    int nl_conn;
    // a low here?
    int nl_family_id;
    int thread_count;
    int threads[256];
    // bitfield
    int nice;
    int nice_err;
    pid_t sid;
    int cpu_affinity:30;
    int yama_enabled:1;
    int is_traced:1;
};

struct proc_signal {
    long signal_pending_mask;
    long signals_blocked;
    long signals_ignored;
    long signals_caught;
    int signals_pending;
};

struct proc_rlim {
    // grok -- a union here?
    // if tracking all the fields in prlimit...
    rlim_t addr_space_cur;
    rlim_t addr_space_max;
    rlim_t core_cur;
    rlim_t core_max;
    rlim_t cpu_cur;
    rlim_t cpu_max;
    rlim_t data_cur;
    rlim_t data_max;
    rlim_t fsize_cur;
    rlim_t fsize_max;
    rlim_t locks_cur;
    rlim_t locks_max;
    rlim_t memlock_cur;
    rlim_t memlock_max;
    rlim_t msgqueue_cur;
    rlim_t msgqueue_max;
    rlim_t nice_cur;
    rlim_t nice_max;
    rlim_t nofile_cur;
    rlim_t nofile_max;
    rlim_t nproc_cur;
    rlim_t nproc_max;
    rlim_t rss_cur;
    rlim_t rss_max;
    rlim_t rtprio_cur;
    rlim_t rtprio_max;
    rlim_t rttime_cur;
    rlim_t rttime_max;
    rlim_t sigpending_cur;
    rlim_t sigpending_max;
    rlim_t stack_cur;
    rlim_t stack_max;
};

// Check if process exists.
__attribute__(( visibility("default") ))
bool is_alive(profile_t *process);

// Count digit places in type int of the process being profiled.
__attribute__(( visibility("default") ))
int pid_digit_places(int pid);

// Function returns the niceness of the pid being profiled.
__attribute__(( visibility("default") ))
void get_process_nice(profile_t *process);

// Sets the niceness of pid.
__attribute__(( visibility("default") ))
void set_pid_nice(profile_t *process, int priority);

#define PROC "/proc/"
#define PROCLEN 6
#define YAMA "/proc/sys/kernel/yama/ptrace_scope"
#define STATUS "/status"
#define COMM "/comm"
#define TASK "/task"
#define FD "/fd/"

// Function to return the name of a pid.
__attribute__(( visibility("default") ))
void pid_name(profile_t *process);

// A function that traverses the "fd" directory of the pid from process
// and loads a fdstat_t struct with stats for each file-descriptor found.
__attribute__(( visibility("default") ))
int process_fd_stats(profile_t *process);

//XXX These definitions are from linux/ioprio.h
#define IOPRIO_WHO_PROCESS 1
#define IOPRIO_SHIFT 13

enum {
    IOPRIO_CLASS_NONE,
    IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE,
    IOPRIO_CLASS_IDLE,
};

#define IOPRIO_PRIO_MASK ((1UL << IOPRIO_SHIFT) - 1)

#define IOPRIO_CLASS(mask) ((mask) >> IOPRIO_SHIFT)
#define IOPRIO_DATA(mask) ((mask) & IOPRIO_PRIO_MASK)
#define IOPRIO_VALUE(class, data) (((class) << IOPRIO_SHIFT) | data)

#define IOPRIO_LEN(class) strlen(class) + IOPRIO_SIZE

// Returns the I/O scheduling class and priority of profiled pid.
__attribute__(( visibility("default") ))
void get_ioprio(profile_t *process);

// Sets the I/O scheduling class and priority of profiled pid.
__attribute__(( visibility("default") ))
void set_ioprio(profile_t *process, int class, int value);

// sets/gets process resource limits.
__attribute__(( visibility("default") ))
void rlim_stat(profile_t *process, int resource, unsigned long *limit);

// gets/sets the current processor affinity in profile_t struct;
__attribute__(( visibility("default") ))
void cpu_affinity(profile_t *process);

__attribute__(( visibility("default") ))
void setcpu_affinity(profile_t *process, int affinity);

// sets the session id field in profile_t struct
__attribute__(( visibility("default") ))
void process_sid(profile_t *process);

// populates the 'threads' array in process struct if any threads are running.
__attribute__(( visibility("default") ))
void running_threads(profile_t *process);

// kills a thread under the current processes group with the provided id.
__attribute__(( visibility("default") ))
void tkill(profile_t *process, int tid);

// sets the 'tgid' field of the processes thread group id number.
__attribute__(( visibility("default") ))
void gettgid(profile_t *process);

// sets the 'uid' field of the processes uid number.
__attribute__(( visibility("default") ))
void getpuid(profile_t *process);

// sets the 'username' field of process provided.
__attribute__(( visibility("default") ))
void getusernam(profile_t *process);

// will find the number voluntary context switches for a process.
__attribute__(( visibility("default") ))
void voluntary_context_switches(profile_t *process);

// will find the number involuntary context switches for a process.
__attribute__(( visibility("default") ))
void involuntary_context_switches(profile_t *process);

// specifies the amount of virtual memory in use by a process.
__attribute__(( visibility("default") ))
void virtual_mem(profile_t *process);
    
#define MAXVAL 64

#define GENLMSG_DATA(gnlmsg) (struct nlattr *) (((char *) &gnlmsg) + \
                                                      GENL_HDRLEN)
#define NLA_DATA(nla) (void *) ((char *) nla + NLA_HDRLEN)

// Initializer for the profile_t type.
__attribute__(( visibility("default") ))
profile_t *init_profile(int pid);

// Free memory used by a profile_t type file descriptors field.
__attribute__(( visibility("default") ))
void free_profile_fd(profile_t *process);

// Free memory used by a profile_t type.
__attribute__(( visibility("default") ))
void free_profile(profile_t *process);

// Checks if the yama security is enabled
__attribute__(( visibility("default") ))
int yama_enabled(void);

// Check if the process is being traced
__attribute__(( visibility("default") ))
int is_traced(profile_t *process);

// Gets the current tracer's pid
__attribute__(( visibility("default") ))
void get_trace_pid(profile_t *process);

// Gets the number of pending signals for the process
__attribute__(( visibility("default") ))
void get_pending_signals(profile_t *process);

// Gets the mask of the currently queued signals for the process
__attribute__(( visibility("default") ))
void get_pending_signals_mask(profile_t *process);

// Gets the blocked signals for the process
__attribute__(( visibility("default") ))
void get_signals_blocked(profile_t *process);

// Gets the signals ignored for the process
__attribute__(( visibility("default") ))
void get_signals_ignored(profile_t *process);

// Gets the signals caught for the process
__attribute__(( visibility("default") ))
void get_signals_caught(profile_t *process);

// Gets the start time for the process
__attribute__(( visibility("default") ))
void get_start_time(profile_t *process);

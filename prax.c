#include "prax.h"

#include <pwd.h>
#include <stdio.h>
#include <ctype.h>
#include <sched.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <limits.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/syscall.h>


int is_alive(profile_t *process)
{
    DIR *proc_dir = opendir(PROC);
    struct dirent *cur_proc = malloc(sizeof *cur_proc);
    while ((cur_proc = readdir(proc_dir))) {
        if (cur_proc->d_type == DT_DIR && 
            !(strcmp(cur_proc->d_name, process->pidstr))) {
            closedir(proc_dir);
            return 0;
        }
    }
    closedir(proc_dir);
    return -1;
}

char *construct_path(int pathparts, ...)
{
    va_list path;
    va_start(path, pathparts);
    int args;
    size_t pathlen;
    char *part;
    char *partial_path;
    char *pathname = calloc(sizeof(char) * PATH_MAX, sizeof(char));
    for (args=0; args < pathparts; args++) {
        part = va_arg(path, char *);
        pathlen = strlen(part) + strlen(pathname) + 1;
        partial_path = pathname;
        pathname = calloc(sizeof(char) * PATH_MAX, sizeof(char));
        snprintf(pathname, pathlen, "%s%s", partial_path, part);
    }
    va_end(path);
    return pathname;
}

void pid_name(profile_t *process)
{
    int alive = is_alive(process);
    if (alive == -1) {
        process->name = NULL;
    } else {
        char *path = construct_path(3, PROC, process->pidstr, COMM);
        FILE *proc = fopen(path, "r");
        char *name = NULL;
        size_t n = 0;
        getline(&name, &n, proc);
        fclose(proc);
        name[strlen(name) - 1] = '\0';
        process->name = name;
    }
}

int process_fd_stats(profile_t *process)
{
    char *fullpath;
    char *buf;
    int open_fd;
    char *fdpath = construct_path(3, PROC, process->pidstr, FD);

    DIR *fd_dir = opendir(fdpath);
    if (!fd_dir) 
        return -1;

    struct dirent *files = malloc(sizeof *files);

    process->fd = malloc(sizeof *(process->fd));
    fdstats_t *curr = process->fd;
    while ((files = readdir(fd_dir))) {
        if (files->d_type == DT_LNK) {
            fullpath = construct_path(2, fdpath, files->d_name);
            open_fd = open(fullpath, O_RDONLY);
            if (open_fd != -1) {
                buf = calloc(sizeof(char) * LINKBUFSIZ, sizeof(char));
                readlink(fullpath, buf, LINKBUFSIZ);
                curr->file = buf;
                curr->file_stats = malloc(sizeof *(curr->file_stats));
                fstat(open_fd, curr->file_stats);
                curr->next_fd = malloc(sizeof *(curr->next_fd));
                curr = curr->next_fd;
                curr->file = NULL;
            }
            free(fullpath);
        }
    }
    return 0;
}

void get_pid_nice(profile_t *process)
{
    int nice_value;
    nice_value = getpriority(PRIO_PROCESS, process->pid);
    process->nice = nice_value;
}

void set_pid_nice(profile_t *process, int priority)
{
    int ret;
    ret = setpriority(PRIO_PROCESS, process->pid, priority);
    if (ret == -1)
        process->nice = ret;
    else
        process->nice = priority;
}

void get_ioprio(profile_t *process)
{
    int ioprio;
    int ioprio_class_num;
    char *class_name;
    int ioprio_nice;
    size_t priolen;
    char *priority;
    char *ioprio_class[4] = {"none/", "rt/", "be/", "idle/"};
    ioprio = syscall(GETIOPRIO, IOPRIO_WHO_PROCESS, process->pid);
    if (ioprio == -1) { 
        process->io_nice = NULL;
    } else {
        get_pid_nice(process);
        ioprio_class_num = IOPRIO_CLASS(ioprio);
        class_name = ioprio_class[ioprio_class_num];
        ioprio_nice = (process->nice + 20) / 5;
        priolen = strlen(class_name) + IOPRIO_SIZE + 1;
        priority = calloc(priolen, sizeof(char));
        snprintf(priority, priolen, "%s%d", class_name, ioprio_nice);
        process->io_nice = priority;
    }
}

void set_ioprio(profile_t *process, int class, int value)
{
    int ioprio, setioprio;
    ioprio = IOPRIO_VALUE(class, value);
    setioprio = syscall(SETIOPRIO, IOPRIO_WHO_PROCESS, 
                                  process->pid, ioprio);
    if (setioprio == -1)
        process->io_nice = NULL;
    else
        get_ioprio(process);
}

void cpu_affinity(profile_t *process)
{
    int ret;
    cpu_set_t procset;
    size_t procsize;

    procsize = sizeof procset;
    ret = sched_getaffinity(process->pid, procsize, &procset);
    if (ret == -1) 
        process->cpu_affinity = -1;
    else 
        process->cpu_affinity = CPU_COUNT(&procset);
}

void setcpu_affinity(profile_t *process, int affinity)
{
    int ret, i;
    cpu_set_t procset;
    size_t procsize;

    CPU_ZERO(&procset);
    for (i=0; i < affinity; CPU_SET(i++, &procset))
        ;
    procsize = sizeof procset;
    
    ret = sched_setaffinity(process->pid, procsize, &procset);
    if (ret == -1) 
        process->cpu_affinity = -1;
    else 
        process->cpu_affinity = affinity;
}

void process_sid(profile_t *process)
{
    pid_t sid;
    sid = getsid(process->pid);
    process->sid = sid;
}

void rlim_stat(profile_t *process, int resource, unsigned long *lim)
{
    struct rlimit *limits = malloc(sizeof *limits);
    prlimit(process->pid, resource, NULL, limits);
    if (lim) {
        limits->rlim_cur = *lim;
        prlimit(process->pid, resource, limits, NULL);
    }
    switch (resource) {
        case(RLIMIT_AS): 
            process->addr_space_cur = limits->rlim_cur;
            process->addr_space_max = limits->rlim_max;
            break;
        case(RLIMIT_CORE):
            process->core_cur = limits->rlim_cur;
            process->core_max = limits->rlim_max;
            break;
        case(RLIMIT_CPU):
            process->cpu_cur = limits->rlim_cur;
            process->cpu_max = limits->rlim_max;
            break;
        case(RLIMIT_DATA):
            process->data_cur = limits->rlim_cur;
            process->data_max = limits->rlim_max;
            break;
        case(RLIMIT_FSIZE):
            process->fsize_cur = limits->rlim_cur;
            process->fsize_max = limits->rlim_max;
            break;
        case(RLIMIT_LOCKS):
            process->locks_cur = limits->rlim_cur;
            process->locks_max = limits->rlim_max;
            break;
        case(RLIMIT_MEMLOCK):
            process->memlock_cur = limits->rlim_cur;
            process->memlock_max = limits->rlim_max;
            break;
        case(RLIMIT_MSGQUEUE):
            process->msgqueue_cur = limits->rlim_cur;
            process->msgqueue_max = limits->rlim_max;
            break;
        case(RLIMIT_NICE):
            process->nice_cur = limits->rlim_cur;
            process->nice_max = limits->rlim_max;
            break;
        case(RLIMIT_NOFILE):
            process->nofile_cur = limits->rlim_cur;
            process->nofile_max = limits->rlim_max;
            break;
        case(RLIMIT_NPROC):
            process->nproc_cur = limits->rlim_cur;
            process->nproc_max = limits->rlim_max;
            break;
        case(RLIMIT_RSS):
            process->rss_cur = limits->rlim_cur;
            process->rss_max = limits->rlim_max;
            break;
        case(RLIMIT_RTPRIO):
            process->rtprio_cur = limits->rlim_cur;
            process->rtprio_max = limits->rlim_max;
            break;
        case(RLIMIT_SIGPENDING):
            process->sigpending_cur = limits->rlim_cur;
            process->sigpending_max = limits->rlim_max;
            break;
        case(RLIMIT_STACK):
            process->stack_cur = limits->rlim_cur;
            process->stack_max = limits->rlim_max;
            break;
    }        
}

void running_threads(profile_t *process)
{
    int tid;
    struct dirent *task;
    char *path = construct_path(3, PROC, process->pidstr, TASK);
    DIR *task_dir = opendir(path);
    int thread_cnt = 0;
    while ((task = readdir(task_dir))) {
        if (!(ispunct(*(task->d_name)))) {
            tid = atoi(task->d_name);
            process->threads[thread_cnt++] = tid;
        }
    }
    process->thread_count = thread_cnt;
}

void tkill(profile_t *process, int tid)
{
    int ret;
    ret = syscall(TGKILL, process->tgid, tid, SIGTERM);
    if (ret == -1)
        printf("Thread kill failed :: id - %d\n", tid);
}

char *parse_status_fields(char *pid, char *field)
{
    int i, l;
    FILE *fp;
    size_t n;
    size_t fieldlen = strlen(field);
    
    char *id = malloc(sizeof(char) * 64);
    char attr[fieldlen];
    char *path;
    char *line = malloc(sizeof(char) * LINE_SZ);
    
    path = construct_path(3, PROC, pid, STATUS);
    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("Error: %s\n", strerror(errno));
        return NULL;
    }
    
    while (getline(&line, &n, fp)) {
        for (l=0; l < fieldlen; l++) 
            attr[l] = *(line + l);
        attr[l] = '\0';
        if (!(strcmp(field, attr))) { 
            i = 0;
            for (;!(isdigit(*(line + l))); ++l) 
                ;
            for (;*(line + l) != '\n'; ++l) 
                id[i++] = *(line + l);
            id[i] = '\0';
            return id;
        }
    }
    printf("Field not found: %s\n", field);
    return NULL;
}

void gettgid(profile_t *process)
{
    char *tgid_name = "Tgid";
    char *tgid = parse_status_fields(process->pidstr, tgid_name); 
    if (tgid)
        process->tgid = atoi(tgid); 
}

void getpuid(profile_t *process)
{
    char *uid_name = "Uid";
    char *uid = parse_status_fields(process->pidstr, uid_name);
    if (uid)
        process->uid = atoi(uid);
}

void getusernam(profile_t *process)
{
    if (!process->uid)
        getpuid(process);
    struct passwd *username = getpwuid(process->uid);
    process->username = username->pw_name;
}

void voluntary_context_switches(profile_t *process)
{
    char *vol_switch = "voluntary_ctxt_switches";
    char *vswitch = parse_status_fields(process->pidstr, vol_switch);
    if (vswitch) 
        process->vol_ctxt_swt = atol(vswitch);
}

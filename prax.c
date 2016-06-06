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
#include <stdbool.h>
#include <sys/time.h>
#include <sys/syscall.h>


bool is_alive(profile_t *process)
{
    char *proc_dir_path = malloc(sizeof(char) * PATH_MAX + 1);
    if (!proc_dir_path)
        return false;

    snprintf(proc_dir_path, PATH_MAX, "%s%s", PROC, process->pidstr);
    DIR *proc_dir_handle = opendir(proc_dir_path);
    bool alive = proc_dir_handle ? true : false;

    if (alive)
        closedir(proc_dir_handle);

    free(proc_dir_path);
    return alive;
}

char *construct_path(int pathparts, ...)
{
    va_list part;
    int args;
    char *path_part, *pathname;

    pathname = NULL;
    va_start(part, pathparts);

    for (args=0; args < pathparts; args++) {
        path_part = (char *) va_arg(part, char *);
        if (pathname == NULL)
            pathname = calloc(sizeof(char) * strlen(path_part) + 1, sizeof(char));
        else 
            pathname = realloc(pathname, strlen(pathname) + strlen(path_part) + 1);
        strcat(pathname, path_part);
    }

    va_end(part);

    return pathname;
}

void pid_name(profile_t *process)
{
    FILE *proc;
    size_t n;
    char *name, *path;

    if (is_alive(process)) {
        path = construct_path(3, PROC, process->pidstr, COMM);

        proc = fopen(path, "r");
        if (proc == NULL) {
            free(path);
            process->name = NULL;
            return;
        }

        name = NULL;
        n = 0;

        getline(&name, &n, proc);
        fclose(proc);

        name[strlen(name) - 1] = '\0';
        process->name = name;
        free(path);
        return;
    }

    process->name = NULL;

    return;
}

int process_fd_stats(profile_t *process)
{
    char *fullpath, *buf, *fdpath;
    struct dirent *files;
    int open_fd;
    DIR *fd_dir;
    fdstats_t *curr;

    fdpath = construct_path(3, PROC, process->pidstr, FD);

    fd_dir = opendir(fdpath);
    if (!fd_dir) 
        return -1;

    process->fd = malloc(sizeof *(process->fd));
    curr = process->fd;

    while ((files = readdir(fd_dir))) {
        if (files->d_type == DT_LNK) {
 
            fullpath = construct_path(2, fdpath, files->d_name);
            open_fd = open(fullpath, O_RDONLY);
            buf = NULL;
            curr->file_stats = NULL;
            curr->next_fd = NULL;

            if (open_fd == -1) {
                free(fullpath); 
                fullpath = NULL;
                continue;
            }

            buf = calloc(sizeof(char) * LINKBUFSIZ, sizeof(char));
            if (buf == NULL) 
                goto error;

            readlink(fullpath, buf, LINKBUFSIZ);
            curr->file = buf;

            curr->file_stats = malloc(sizeof *curr->file_stats);
            if (curr->file_stats == NULL)
                goto error;

            fstat(open_fd, curr->file_stats);

            curr->next_fd = malloc(sizeof *curr->next_fd);
            if (curr->next_fd == NULL)
                goto error;

            curr = curr->next_fd;
            curr->file = NULL;
            close(open_fd);
            free(fullpath);
            fullpath = NULL;
        }
    }

    if (fullpath)
        free(fullpath);
    if (fd_dir)
        closedir(fd_dir);
    free(fdpath);
    return 0;

    error:
        free(fullpath);
        free(fdpath);
        if (buf)
            free(buf);
        if (fd_dir)
            closedir(fd_dir);
        if (curr->file_stats)
            free(curr->file_stats);
        if (curr->next_fd)
            free(curr->next_fd);

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

    process->ioprio= NULL;
    ioprio = syscall(GETIOPRIO, IOPRIO_WHO_PROCESS, process->pid);
    if (ioprio == -1)
        return;

    if (IOPRIO_CLASS(ioprio) != 0) {
        process->ioprio = malloc(sizeof(char) * 
                          IOPRIO_LEN(class[IOPRIO_CLASS(ioprio)]));
        snprintf(process->ioprio, IOPRIO_LEN(class[IOPRIO_CLASS(ioprio)]), 
                 "%s%ld", class[IOPRIO_CLASS(ioprio)], IOPRIO_DATA(ioprio));
    } else
        get_ioprio_nice(process, ioprio);
}

void get_ioprio_nice(profile_t *process, int ioprio)
{
    int ioprio_level, prio;

    get_pid_nice(process);
    ioprio_level = (process->nice + 20) / 5;
    prio = sched_getscheduler(process->pid);

    if (prio == SCHED_FIFO || prio == SCHED_RR) {        
        process->ioprio = malloc(sizeof(char) * IOPRIO_LEN(class[1]));
        snprintf(process->ioprio, IOPRIO_LEN(class[1]), 
                 "%s%d", class[1], ioprio_level);
    } else if (prio == SCHED_OTHER) {
        process->ioprio = malloc(sizeof(char) * IOPRIO_LEN(class[2]));
        snprintf(process->ioprio, IOPRIO_LEN(class[2]), 
                 "%s%d", class[2], ioprio_level);
    } else {
        process->ioprio = malloc(sizeof(char) * strlen(class[3]) + 1);
        snprintf(process->ioprio, IOPRIO_LEN(class[3]), "%s", class[3]);
    }
}

void set_ioprio(profile_t *process, int class, int value)
{
    int ioprio, setioprio;
    ioprio = IOPRIO_VALUE(class, value);
    setioprio = syscall(SETIOPRIO, IOPRIO_WHO_PROCESS, 
                                  process->pid, ioprio);
    if (setioprio == -1)
        process->ioprio = NULL;
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
    struct rlimit limits;
    prlimit(process->pid, resource, NULL, &limits);

    if (lim) {
        limits.rlim_cur = *lim;
        prlimit(process->pid, resource, &limits, NULL);
    }

    switch (resource) {
        case(RLIMIT_AS): 
            process->addr_space_cur = limits.rlim_cur;
            process->addr_space_max = limits.rlim_max;
            break;
        case(RLIMIT_CORE):
            process->core_cur = limits.rlim_cur;
            process->core_max = limits.rlim_max;
            break;
        case(RLIMIT_CPU):
            process->cpu_cur = limits.rlim_cur;
            process->cpu_max = limits.rlim_max;
            break;
        case(RLIMIT_DATA):
            process->data_cur = limits.rlim_cur;
            process->data_max = limits.rlim_max;
            break;
        case(RLIMIT_FSIZE):
            process->fsize_cur = limits.rlim_cur;
            process->fsize_max = limits.rlim_max;
            break;
        case(RLIMIT_LOCKS):
            process->locks_cur = limits.rlim_cur;
            process->locks_max = limits.rlim_max;
            break;
        case(RLIMIT_MEMLOCK):
            process->memlock_cur = limits.rlim_cur;
            process->memlock_max = limits.rlim_max;
            break;
        case(RLIMIT_MSGQUEUE):
            process->msgqueue_cur = limits.rlim_cur;
            process->msgqueue_max = limits.rlim_max;
            break;
        case(RLIMIT_NICE):
            process->nice_cur = limits.rlim_cur;
            process->nice_max = limits.rlim_max;
            break;
        case(RLIMIT_NOFILE):
            process->nofile_cur = limits.rlim_cur;
            process->nofile_max = limits.rlim_max;
            break;
        case(RLIMIT_NPROC):
            process->nproc_cur = limits.rlim_cur;
            process->nproc_max = limits.rlim_max;
            break;
        case(RLIMIT_RSS):
            process->rss_cur = limits.rlim_cur;
            process->rss_max = limits.rlim_max;
            break;
        case(RLIMIT_RTPRIO):
            process->rtprio_cur = limits.rlim_cur;
            process->rtprio_max = limits.rlim_max;
            break;
        case(RLIMIT_SIGPENDING):
            process->sigpending_cur = limits.rlim_cur;
            process->sigpending_max = limits.rlim_max;
            break;
        case(RLIMIT_STACK):
            process->stack_cur = limits.rlim_cur;
            process->stack_max = limits.rlim_max;
            break;
    }        
}

void running_threads(profile_t *process)
{
    int tid, thread_cnt;
    struct dirent *task;
    DIR *task_dir;
    char *path;
    
    path = construct_path(3, PROC, process->pidstr, TASK);
    thread_cnt = 0;

    task_dir = opendir(path);
    if (task_dir == NULL)
        goto count;
   
    while ((task = readdir(task_dir))) {
        if (!(ispunct(*(task->d_name)))) {
            tid = atoi(task->d_name);
            process->threads[thread_cnt++] = tid;
        }
    }

    closedir(task_dir);
    
    count:
        process->thread_count = thread_cnt;

    free(path);
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
    size_t n, fieldlen;
    
    char *line, *path;    
    path = construct_path(3, PROC, pid, STATUS);

    line = NULL;
    fp = fopen(path, "r");
    if (fp == NULL) 
        goto file_error;

    for (n=0, i=0, l=0, fieldlen = strlen(field);
         getline(&line, &n, fp) != -1; 
         *(line + fieldlen) = '\0') {
        if (!(strcmp(field, line))) {
            for (;!(isdigit(*(line + i))); ++i) 
                ;
            for (;isdigit(*(line + i)); ++i, ++l) 
                *(line + l) = *(line + i);
            *(line + l) = '\0';
            goto line_found;
        }
    }

    free(line);
    line = NULL;

    line_found:
        fclose(fp);

    file_error:
        free(path);

    return line;
}

void gettgid(profile_t *process)
{
    char *tgid_name = "Tgid";
    char *tgid = parse_status_fields(process->pidstr, tgid_name); 
    if (tgid) {
        process->tgid = atoi(tgid); 
        free(tgid);
    }
}

void getpuid(profile_t *process)
{
    char *uid_name = "Uid";
    char *uid = parse_status_fields(process->pidstr, uid_name);
    if (uid) {
        process->uid = atoi(uid);
        free(uid);
    }
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
    if (vswitch) { 
        process->vol_ctxt_swt = atol(vswitch);
        free(vswitch);
    }
}

void involuntary_context_switches(profile_t *process)
{
    char *invol_switch = "nonvoluntary_ctxt_switches";
    char *ivswitch = parse_status_fields(process->pidstr, invol_switch);
    if (ivswitch) {
        process->invol_ctxt_swt = atol(ivswitch);
        free(ivswitch);
    }
}

void virtual_mem(profile_t *process)
{
    char *virtual_memory = "VmSize";
    char *total_memory = parse_status_fields(process->pidstr, virtual_memory);
    if (total_memory) {
        process->vmem = atol(total_memory);
        free(total_memory);
    }
}

profile_t init_profile(void)
{
    profile_t process = {
        .pidstr = NULL,
        .name = NULL,
        .username = NULL,
        .ioprio = NULL,
        .fd = NULL,
    };

    return process;    
}

void free_profile_fd(profile_t *process)
{
    fdstats_t *curr;
    fdstats_t *next;
    
    for (curr=process->fd; curr; curr=next) {
        next = curr->next_fd;
        free(curr->file);
        free(curr->file_stats);
        free(curr);
    }
}

void free_profile(profile_t *process)
{
    if (!process)
        return;

    if (process->pidstr)
        free(process->pidstr);

    if (process->name)
        free(process->name);

    if (process->ioprio)
        free(process->ioprio);

    if (process->fd)
        free_profile_fd(process);
}

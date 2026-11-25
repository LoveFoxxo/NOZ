
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>


static void ngx_console_init(ngx_cycle_t *cycle);
static int __stdcall ngx_console_handler(u_long type);
static ngx_int_t ngx_create_signal_events(ngx_cycle_t *cycle);
static ngx_int_t ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t type);
static void ngx_reopen_worker_processes(ngx_cycle_t *cycle);
static void ngx_quit_worker_processes(ngx_cycle_t *cycle, ngx_uint_t old);
static void ngx_terminate_worker_processes(ngx_cycle_t *cycle);
static ngx_uint_t ngx_reap_worker(ngx_cycle_t *cycle, HANDLE h);
static void ngx_master_process_exit(ngx_cycle_t *cycle);
static void ngx_worker_process_cycle(ngx_cycle_t *cycle, char *mevn);
static void ngx_worker_process_exit(ngx_cycle_t *cycle);
static ngx_thread_value_t __stdcall ngx_worker_thread(void *data);
static ngx_thread_value_t __stdcall ngx_cache_manager_thread(void *data);
static void ngx_cache_manager_process_handler(void);
static ngx_thread_value_t __stdcall ngx_cache_loader_thread(void *data);


ngx_uint_t     ngx_process;
ngx_uint_t     ngx_worker;
ngx_pid_t      ngx_pid;
ngx_pid_t      ngx_parent;

ngx_uint_t     ngx_inherited;
ngx_pid_t      ngx_new_binary;

sig_atomic_t   ngx_terminate;
sig_atomic_t   ngx_quit;
sig_atomic_t   ngx_reopen;
sig_atomic_t   ngx_reconfigure;
ngx_uint_t     ngx_exiting;


HANDLE         ngx_master_process_event;
char           ngx_master_process_event_name[NGX_PROCESS_SYNC_NAME];

static HANDLE  ngx_stop_event;
static char    ngx_stop_event_name[NGX_PROCESS_SYNC_NAME];
static HANDLE  ngx_quit_event;
static char    ngx_quit_event_name[NGX_PROCESS_SYNC_NAME];
static HANDLE  ngx_reopen_event;
static char    ngx_reopen_event_name[NGX_PROCESS_SYNC_NAME];
static HANDLE  ngx_reload_event;
static char    ngx_reload_event_name[NGX_PROCESS_SYNC_NAME];

HANDLE         ngx_cache_manager_mutex;
char           ngx_cache_manager_mutex_name[NGX_PROCESS_SYNC_NAME];
HANDLE         ngx_cache_manager_event;


void
ngx_master_process_cycle(ngx_cycle_t *cycle)
{
    u_long      nev, ev, timeout;
    ngx_err_t   err;
    ngx_int_t   n;
    ngx_msec_t  timer;
    ngx_uint_t  live;
    HANDLE      events[MAXIMUM_WAIT_OBJECTS];

    ngx_sprintf((u_char *) ngx_master_process_event_name,
                "ngx_master_%s%Z", ngx_unique);

    if (ngx_process == NGX_PROCESS_WORKER) {
        ngx_worker_process_cycle(cycle, ngx_master_process_event_name);
        return;
    }


    ngx_console_init(cycle);

    SetEnvironmentVariable("ngx_unique", ngx_unique);

    ngx_master_process_event = CreateEvent(NULL, 1, 0,
                                           ngx_master_process_event_name);
    if (ngx_master_process_event == NULL) {
        exit(2);
    }

    if (ngx_create_signal_events(cycle) != NGX_OK) {
        exit(2);
    }

    ngx_sprintf((u_char *) ngx_cache_manager_mutex_name,
                "ngx_cache_manager_mutex_%s%Z", ngx_unique);

    ngx_cache_manager_mutex = CreateMutex(NULL, 0,
                                          ngx_cache_manager_mutex_name);
    if (ngx_cache_manager_mutex == NULL) {
        exit(2);
    }


    events[0] = ngx_stop_event;
    events[1] = ngx_quit_event;
    events[2] = ngx_reopen_event;
    events[3] = ngx_reload_event;

    ngx_close_listening_sockets(cycle);

    if (ngx_start_worker_processes(cycle, NGX_PROCESS_RESPAWN) == 0) {
        exit(2);
    }

    timer = 0;
    timeout = INFINITE;

    for ( ;; ) {

        nev = 4;
        for (n = 0; n < ngx_last_process; n++) {
            if (ngx_processes[n].handle) {
                events[nev++] = ngx_processes[n].handle;
            }
        }

        if (timer) {
            timeout = timer > ngx_current_msec ? timer - ngx_current_msec : 0;
        }

        ev = WaitForMultipleObjects(nev, events, 0, timeout);

        err = ngx_errno;
        ngx_time_update();


        if (ev == WAIT_OBJECT_0) {

            if (ResetEvent(ngx_stop_event) == 0) {
            }

            if (timer == 0) {
                timer = ngx_current_msec + 5000;
            }

            ngx_terminate = 1;
            ngx_quit_worker_processes(cycle, 0);

            continue;
        }

        if (ev == WAIT_OBJECT_0 + 1) {

            if (ResetEvent(ngx_quit_event) == 0) {
            }

            ngx_quit = 1;
            ngx_quit_worker_processes(cycle, 0);

            continue;
        }

        if (ev == WAIT_OBJECT_0 + 2) {

            if (ResetEvent(ngx_reopen_event) == 0) {
            }

            ngx_reopen_files(cycle, -1);
            ngx_reopen_worker_processes(cycle);

            continue;
        }

        if (ev == WAIT_OBJECT_0 + 3) {

            if (ResetEvent(ngx_reload_event) == 0) {
            }

            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL) {
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            ngx_cycle = cycle;

            ngx_close_listening_sockets(cycle);

            if (ngx_start_worker_processes(cycle, NGX_PROCESS_JUST_RESPAWN)) {
                ngx_quit_worker_processes(cycle, 1);
            }

            continue;
        }

        if (ev > WAIT_OBJECT_0 + 3 && ev < WAIT_OBJECT_0 + nev) {


            live = ngx_reap_worker(cycle, events[ev]);

            if (!live && (ngx_terminate || ngx_quit)) {
                ngx_master_process_exit(cycle);
            }

            continue;
        }

        if (ev == WAIT_TIMEOUT) {
            ngx_terminate_worker_processes(cycle);

            ngx_master_process_exit(cycle);
        }

        if (ev == WAIT_FAILED) {

            continue;
        }

    }
}


static void
ngx_console_init(ngx_cycle_t *cycle)
{
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ccf->daemon) {
        if (FreeConsole() == 0) {
        }

        return;
    }

    if (SetConsoleCtrlHandler(ngx_console_handler, 1) == 0) {
    }
}


static int __stdcall
ngx_console_handler(u_long type)
{
    char  *msg;

    switch (type) {

    case CTRL_C_EVENT:
        msg = "Ctrl-C pressed, exiting";
        break;

    case CTRL_BREAK_EVENT:
        msg = "Ctrl-Break pressed, exiting";
        break;

    case CTRL_CLOSE_EVENT:
        msg = "console closing, exiting";
        break;

    case CTRL_LOGOFF_EVENT:
        msg = "user logs off, exiting";
        break;

    default:
        return 0;
    }


    if (ngx_stop_event == NULL) {
        return 1;
    }

    if (SetEvent(ngx_stop_event) == 0) {
    }

    return 1;
}


static ngx_int_t
ngx_create_signal_events(ngx_cycle_t *cycle)
{
    ngx_sprintf((u_char *) ngx_stop_event_name,
                "Global\\ngx_stop_%s%Z", ngx_unique);

    ngx_stop_event = CreateEvent(NULL, 1, 0, ngx_stop_event_name);
    if (ngx_stop_event == NULL) {
        return NGX_ERROR;
    }


    ngx_sprintf((u_char *) ngx_quit_event_name,
                "Global\\ngx_quit_%s%Z", ngx_unique);

    ngx_quit_event = CreateEvent(NULL, 1, 0, ngx_quit_event_name);
    if (ngx_quit_event == NULL) {
        return NGX_ERROR;
    }


    ngx_sprintf((u_char *) ngx_reopen_event_name,
                "Global\\ngx_reopen_%s%Z", ngx_unique);

    ngx_reopen_event = CreateEvent(NULL, 1, 0, ngx_reopen_event_name);
    if (ngx_reopen_event == NULL) {
        return NGX_ERROR;
    }


    ngx_sprintf((u_char *) ngx_reload_event_name,
                "Global\\ngx_reload_%s%Z", ngx_unique);

    ngx_reload_event = CreateEvent(NULL, 1, 0, ngx_reload_event_name);
    if (ngx_reload_event == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t type)
{
    ngx_int_t         n;
    ngx_core_conf_t  *ccf;


    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    for (n = 0; n < ccf->worker_processes; n++) {
        if (ngx_spawn_process(cycle, "worker", type) == NGX_INVALID_PID) {
            break;
        }
    }

    return n;
}


static void
ngx_reopen_worker_processes(ngx_cycle_t *cycle)
{
    ngx_int_t  n;

    for (n = 0; n < ngx_last_process; n++) {

        if (ngx_processes[n].handle == NULL) {
            continue;
        }

        if (SetEvent(ngx_processes[n].reopen) == 0) {
        }
    }
}


static void
ngx_quit_worker_processes(ngx_cycle_t *cycle, ngx_uint_t old)
{
    ngx_int_t  n;

    for (n = 0; n < ngx_last_process; n++) {


        if (old && ngx_processes[n].just_spawn) {
            ngx_processes[n].just_spawn = 0;
            continue;
        }

        if (ngx_processes[n].handle == NULL) {
            continue;
        }

        if (SetEvent(ngx_processes[n].quit) == 0) {
        }

        ngx_processes[n].exiting = 1;
    }
}


static void
ngx_terminate_worker_processes(ngx_cycle_t *cycle)
{
    ngx_int_t  n;

    for (n = 0; n < ngx_last_process; n++) {

        if (ngx_processes[n].handle == NULL) {
            continue;
        }

        if (TerminateProcess(ngx_processes[n].handle, 0) == 0) {
        }

        ngx_processes[n].exiting = 1;

        ngx_close_handle(ngx_processes[n].reopen);
        ngx_close_handle(ngx_processes[n].quit);
        ngx_close_handle(ngx_processes[n].term);
        ngx_close_handle(ngx_processes[n].handle);
    }
}


static ngx_uint_t
ngx_reap_worker(ngx_cycle_t *cycle, HANDLE h)
{
    u_long     code;
    ngx_int_t  n;

    for (n = 0; n < ngx_last_process; n++) {

        if (ngx_processes[n].handle != h) {
            continue;
        }

        if (GetExitCodeProcess(h, &code) == 0) {
        }


        ngx_close_handle(ngx_processes[n].reopen);
        ngx_close_handle(ngx_processes[n].quit);
        ngx_close_handle(ngx_processes[n].term);
        ngx_close_handle(h);

        ngx_processes[n].handle = NULL;
        ngx_processes[n].term = NULL;
        ngx_processes[n].quit = NULL;
        ngx_processes[n].reopen = NULL;

        if (!ngx_processes[n].exiting && !ngx_terminate && !ngx_quit) {

            if (ngx_spawn_process(cycle, ngx_processes[n].name, n)
                == NGX_INVALID_PID)
            {

                if (n == ngx_last_process - 1) {
                    ngx_last_process--;
                }
            }
        }

        goto found;
    }


found:

    for (n = 0; n < ngx_last_process; n++) {


        if (ngx_processes[n].handle) {
            return 1;
        }
    }

    return 0;
}


static void
ngx_master_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t  i;

    ngx_delete_pidfile(cycle);

    ngx_close_handle(ngx_cache_manager_mutex);
    ngx_close_handle(ngx_stop_event);
    ngx_close_handle(ngx_quit_event);
    ngx_close_handle(ngx_reopen_event);
    ngx_close_handle(ngx_reload_event);
    ngx_close_handle(ngx_master_process_event);


    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->exit_master) {
            cycle->modules[i]->exit_master(cycle);
        }
    }

    ngx_destroy_pool(cycle->pool);

    exit(0);
}


static void
ngx_worker_process_cycle(ngx_cycle_t *cycle, char *mevn)
{
    char        wtevn[NGX_PROCESS_SYNC_NAME];
    char        wqevn[NGX_PROCESS_SYNC_NAME];
    char        wroevn[NGX_PROCESS_SYNC_NAME];
    HANDLE      mev, events[3];
    u_long      nev, ev;
    ngx_err_t   err;
    ngx_tid_t   wtid, cmtid, cltid;
    ngx_log_t  *log;

    log = cycle->log;


    ngx_sprintf((u_char *) wtevn, "ngx_worker_term_%P%Z", ngx_pid);
    events[0] = CreateEvent(NULL, 1, 0, wtevn);
    if (events[0] == NULL) {
        goto failed;
    }

    ngx_sprintf((u_char *) wqevn, "ngx_worker_quit_%P%Z", ngx_pid);
    events[1] = CreateEvent(NULL, 1, 0, wqevn);
    if (events[1] == NULL) {
        goto failed;
    }

    ngx_sprintf((u_char *) wroevn, "ngx_worker_reopen_%P%Z", ngx_pid);
    events[2] = CreateEvent(NULL, 1, 0, wroevn);
    if (events[2] == NULL) {
        goto failed;
    }

    mev = OpenEvent(EVENT_MODIFY_STATE, 0, mevn);
    if (mev == NULL) {
        goto failed;
    }

    if (SetEvent(mev) == 0) {
        goto failed;
    }


    ngx_sprintf((u_char *) ngx_cache_manager_mutex_name,
                "ngx_cache_manager_mutex_%s%Z", ngx_unique);

    ngx_cache_manager_mutex = OpenMutex(SYNCHRONIZE, 0,
                                        ngx_cache_manager_mutex_name);
    if (ngx_cache_manager_mutex == NULL) {
        goto failed;
    }

    ngx_cache_manager_event = CreateEvent(NULL, 1, 0, NULL);
    if (ngx_cache_manager_event == NULL) {
        goto failed;
    }


    if (ngx_create_thread(&wtid, ngx_worker_thread, NULL, log) != 0) {
        goto failed;
    }

    if (ngx_create_thread(&cmtid, ngx_cache_manager_thread, NULL, log) != 0) {
        goto failed;
    }

    if (ngx_create_thread(&cltid, ngx_cache_loader_thread, NULL, log) != 0) {
        goto failed;
    }

    for ( ;; ) {
        ev = WaitForMultipleObjects(3, events, 0, INFINITE);

        err = ngx_errno;
        ngx_time_update();


        if (ev == WAIT_OBJECT_0) {
            ngx_terminate = 1;

            if (ResetEvent(events[0]) == 0) {
            }

            break;
        }

        if (ev == WAIT_OBJECT_0 + 1) {
            ngx_quit = 1;
            break;
        }

        if (ev == WAIT_OBJECT_0 + 2) {
            ngx_reopen = 1;

            if (ResetEvent(events[2]) == 0) {
            }

            continue;
        }

        if (ev == WAIT_FAILED) {

            goto failed;
        }
    }

    /* wait threads */

    if (SetEvent(ngx_cache_manager_event) == 0) {
    }

    events[1] = wtid;
    events[2] = cmtid;

    nev = 3;

    for ( ;; ) {
        ev = WaitForMultipleObjects(nev, events, 0, INFINITE);

        err = ngx_errno;
        ngx_time_update();


        if (ev == WAIT_OBJECT_0) {
            break;
        }

        if (ev == WAIT_OBJECT_0 + 1) {
            if (nev == 2) {
                break;
            }

            events[1] = events[2];
            nev = 2;
            continue;
        }

        if (ev == WAIT_OBJECT_0 + 2) {
            nev = 2;
            continue;
        }

        if (ev == WAIT_FAILED) {
            break;
        }
    }

    ngx_close_handle(ngx_cache_manager_event);
    ngx_close_handle(events[0]);
    ngx_close_handle(events[1]);
    ngx_close_handle(events[2]);
    ngx_close_handle(mev);

    ngx_worker_process_exit(cycle);

failed:

    exit(2);
}


static ngx_thread_value_t __stdcall
ngx_worker_thread(void *data)
{
    ngx_int_t     n;
    ngx_time_t   *tp;
    ngx_cycle_t  *cycle;

    tp = ngx_timeofday();
    srand((ngx_pid << 16) ^ (unsigned) tp->sec ^ tp->msec);

    cycle = (ngx_cycle_t *) ngx_cycle;

    for (n = 0; cycle->modules[n]; n++) {
        if (cycle->modules[n]->init_process) {
            if (cycle->modules[n]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }

    while (!ngx_quit) {

        if (ngx_exiting) {
            if (ngx_event_no_timers_left() == NGX_OK) {
                break;
            }
        }


        ngx_process_events_and_timers(cycle);

        if (ngx_terminate) {
            return 0;
        }

        if (ngx_quit) {
            ngx_quit = 0;

            if (!ngx_exiting) {
                ngx_exiting = 1;
                ngx_set_shutdown_timer(cycle);
                ngx_close_listening_sockets(cycle);
                ngx_close_idle_connections(cycle);
                ngx_event_process_posted(cycle, &ngx_posted_events);
            }
        }

        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_reopen_files(cycle, -1);
        }
    }


    return 0;
}


static void
ngx_worker_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t         i;
    ngx_connection_t  *c;


    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->exit_process) {
            cycle->modules[i]->exit_process(cycle);
        }
    }

    if (ngx_exiting && !ngx_terminate) {
        c = cycle->connections;
        for (i = 0; i < cycle->connection_n; i++) {
            if (c[i].fd != (ngx_socket_t) -1
                && c[i].read
                && !c[i].read->accept
                && !c[i].read->channel
                && !c[i].read->resolver)
            {
            }
        }
    }

    ngx_destroy_pool(cycle->pool);

    exit(0);
}


static ngx_thread_value_t __stdcall
ngx_cache_manager_thread(void *data)
{
    u_long        ev;
    HANDLE        events[2];
    ngx_err_t     err;
    ngx_cycle_t  *cycle;

    cycle = (ngx_cycle_t *) ngx_cycle;

    events[0] = ngx_cache_manager_event;
    events[1] = ngx_cache_manager_mutex;

    for ( ;; ) {
        ev = WaitForMultipleObjects(2, events, 0, INFINITE);

        err = ngx_errno;
        ngx_time_update();


        if (ev == WAIT_FAILED) {
        }

        /*
         * ev == WAIT_OBJECT_0
         * ev == WAIT_OBJECT_0 + 1
         * ev == WAIT_ABANDONED_0 + 1
         */

        if (ngx_terminate || ngx_quit || ngx_exiting) {
            return 0;
        }

        break;
    }

    for ( ;; ) {

        if (ngx_terminate || ngx_quit || ngx_exiting) {
            break;
        }

        ngx_cache_manager_process_handler();
    }

    if (ReleaseMutex(ngx_cache_manager_mutex) == 0) {
    }

    return 0;
}


static void
ngx_cache_manager_process_handler(void)
{
    u_long        ev;
    ngx_uint_t    i;
    ngx_msec_t    next, n;
    ngx_path_t  **path;

    next = 60 * 60 * 1000;

    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++) {

        if (path[i]->manager) {
            n = path[i]->manager(path[i]->data);

            next = (n <= next) ? n : next;

            ngx_time_update();
        }
    }

    if (next == 0) {
        next = 1;
    }

    ev = WaitForSingleObject(ngx_cache_manager_event, (u_long) next);

    if (ev != WAIT_TIMEOUT) {

        ngx_time_update();

    }
}


static ngx_thread_value_t __stdcall
ngx_cache_loader_thread(void *data)
{
    ngx_uint_t     i;
    ngx_path_t   **path;
    ngx_cycle_t   *cycle;

    ngx_msleep(60000);

    cycle = (ngx_cycle_t *) ngx_cycle;

    path = cycle->paths.elts;
    for (i = 0; i < cycle->paths.nelts; i++) {

        if (ngx_terminate || ngx_quit || ngx_exiting) {
            break;
        }

        if (path[i]->loader) {
            path[i]->loader(path[i]->data);
            ngx_time_update();
        }
    }

    return 0;
}


void
ngx_single_process_cycle(ngx_cycle_t *cycle)
{
    ngx_tid_t  tid;

    ngx_console_init(cycle);

    if (ngx_create_signal_events(cycle) != NGX_OK) {
        exit(2);
    }

    if (ngx_create_thread(&tid, ngx_worker_thread, NULL, cycle->log) != 0) {
        /* fatal */
        exit(2);
    }

    /* STUB */
    WaitForSingleObject(ngx_stop_event, INFINITE);
}


ngx_int_t
ngx_os_signal_process(ngx_cycle_t *cycle, char *sig, ngx_pid_t pid)
{
    HANDLE     ev;
    ngx_int_t  rc;
    char       evn[NGX_PROCESS_SYNC_NAME];

    ngx_sprintf((u_char *) evn, "Global\\ngx_%s_%P%Z", sig, pid);

    ev = OpenEvent(EVENT_MODIFY_STATE, 0, evn);
    if (ev == NULL) {
        return 1;
    }

    if (SetEvent(ev) == 0) {
        rc = 1;

    } else {
        rc = 0;
    }

    ngx_close_handle(ev);

    return rc;
}


void
ngx_close_handle(HANDLE h)
{
    if (CloseHandle(h) == 0) {
    }
}

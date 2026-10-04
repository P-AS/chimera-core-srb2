/* i_threads.c - SRB2's threads (upstream's sdl/i_threads.c) in a machine with
 * one thread: there are none to start (I_can_thread is false, so upstream
 * does its threaded work - HTTP downloads, the master server - not at all,
 * or in line), and a mutex is never contended. */
#include "i_threads.h"

int I_can_thread(void) { return 0; }
void I_start_threads(void) {}
void I_stop_threads(void) {}
int I_spawn_thread(const char *name, I_thread_fn fn, void *userdata)
{
	(void)name; (void)fn; (void)userdata;
	return 0;
}
int I_thread_is_stopped(void) { return 1; }
void I_lock_mutex(I_mutex *m) { (void)m; }
void I_unlock_mutex(I_mutex m) { (void)m; }
void I_hold_cond(I_cond *c, I_mutex m) { (void)c; (void)m; }
void I_wake_one_cond(I_cond *c) { (void)c; }
void I_wake_all_cond(I_cond *c) { (void)c; }

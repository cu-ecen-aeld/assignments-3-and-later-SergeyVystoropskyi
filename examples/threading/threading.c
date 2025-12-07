#include "threading.h"
 
#include <unistd.h>     // usleep
#include <stdlib.h>     // malloc, free
#include <pthread.h>    // pthread_*
#include <stdbool.h>    // bool
 
/**
 * Internal struct used only in this file.
 * It embeds the public thread_data as the first member
 * so that a pointer to this struct can be treated as
 * a pointer to struct thread_data by the tests.
 */
struct thread_info {
    struct thread_data pub;          // must be first
    pthread_mutex_t *mutex;
    int wait_to_obtain_ms;
    int wait_to_release_ms;
};
 
/**
 * This function is run in the new thread created by
 * start_thread_obtaining_mutex().
 *
 * It will:
 *   1. Sleep for wait_to_obtain_ms milliseconds
 *   2. Lock the supplied mutex
 *   3. Sleep for wait_to_release_ms milliseconds
 *   4. Unlock the mutex
 *
 * It sets thread_complete_success = true only if all operations succeed.
 */
void* threadfunc(void* thread_param)
{
    if (thread_param == NULL) {
        return NULL;
    }
 
    struct thread_info *info = (struct thread_info *)thread_param;
    struct thread_data *data = &info->pub;
 
    // Assume failure unless everything succeeds
    data->thread_complete_success = false;
 
    // 1) Wait before obtaining the mutex
    usleep((useconds_t)info->wait_to_obtain_ms * 1000);
 
    // 2) Lock the mutex
    if (pthread_mutex_lock(info->mutex) != 0) {
        return data;    // failure, keep thread_complete_success = false
    }
 
    // 3) Hold the mutex for some time
    usleep((useconds_t)info->wait_to_release_ms * 1000);
 
    // 4) Unlock the mutex
    if (pthread_mutex_unlock(info->mutex) != 0) {
        return data;    // failure
    }
 
    // If we got here, everything worked
    data->thread_complete_success = true;
 
    // Return pointer that the joiner treats as struct thread_data*
    return data;
}
 
/**
 * Start a new thread which will:
 *   - Sleep wait_to_obtain_ms
 *   - Lock the given mutex
 *   - Sleep wait_to_release_ms
 *   - Unlock the mutex
 *
 * Returns true if pthread_create succeeded, false otherwise.
 */
bool start_thread_obtaining_mutex(pthread_t *thread,
                                  pthread_mutex_t *mutex,
                                  int wait_to_obtain_ms,
                                  int wait_to_release_ms)
{
    if (thread == NULL || mutex == NULL) {
        return false;
    }
 
    struct thread_info *info = malloc(sizeof(struct thread_info));
    if (info == NULL) {
        return false;
    }
 
    info->pub.thread_complete_success = false;
    info->mutex              = mutex;
    info->wait_to_obtain_ms  = wait_to_obtain_ms;
    info->wait_to_release_ms = wait_to_release_ms;
 
    int rc = pthread_create(thread, NULL, threadfunc, info);
    if (rc != 0) {
        free(info);
        return false;
    }
 
    // The joiner can do:
    //   struct thread_data *res;
    //   pthread_join(thread, (void **)&res);
    //   free(res);
    //
    // and 'res' will be the same pointer we allocated (info),
    // because pub is the first struct member.
    return true;
}

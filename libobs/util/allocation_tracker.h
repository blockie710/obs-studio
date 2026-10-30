#ifndef OBS_ALLOCATION_TRACKER_H
#define OBS_ALLOCATION_TRACKER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start tracking allocations. After this call, allocations on the audio thread
 * will be recorded until tracker_stop() is called.
 */
void tracker_start(void);

/**
 * Stop tracking allocations.
 */
void tracker_stop(void);

/**
 * Mark the current thread as the audio thread. Allocations on this thread
 * while tracking is active will be considered violations.
 */
void tracker_mark_audio_thread(void);

/**
 * Unmark the current thread as the audio thread.
 */
void tracker_unmark_audio_thread(void);

/**
 * Structure representing an allocation violation.
 */
typedef struct {
    void* ptr;              ///< Pointer to the allocated memory
    size_t size;            ///< Size of the allocation in bytes
    void** stack_trace;     ///< Array of return addresses (stack trace), terminated by NULL
    int stack_depth;        ///< Number of valid entries in stack_trace
} allocation_violation_t;

/**
 * Get the list of allocation violations recorded since tracker_start().
 * The returned array must be freed with tracker_free_violations().
 *
 * @param count Output parameter receiving the number of violations.
 * @return Pointer to an array of allocation_violation_t, or NULL if no violations.
 */
allocation_violation_t* tracker_get_violations(int* count);

/**
 * Free the array returned by tracker_get_violations().
 *
 * @param violations The array to free.
 * @param count The number of violations in the array (returned by tracker_get_violations).
 */
void tracker_free_violations(allocation_violation_t* violations, int count);

#ifdef __cplusplus
}
#endif

#endif // OBS_ALLOCATION_TRACKER_H
#include "allocation_tracker.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment(lib, "dbghelp.lib")
#else
    #include <execinfo.h>
    #include <unistd.h>
#endif

/* Platform-specific retrieval of real malloc and free */
#if defined(_WIN32)
    static HMODULE get_msvcrt_module(void) {
        /* Try to get the handle of the CRT DLL */
        HMODULE hModule = GetModuleHandleA("msvcrt.dll");
        if (!hModule) {
            hModule = LoadLibraryA("msvcrt.dll");
        }
        return hModule;
    }
#endif

/* Initialize the real malloc and free function pointers */
static void init_real_functions(void) {
    static int initialized = 0;
    if (initialized) return;

#if defined(_WIN32)
    HMODULE hModule = get_msvcrt_module();
    if (hModule) {
        real_malloc = (void* (*)(size_t))GetProcAddress(hModule, "malloc");
        real_free = (void (*)(void*))GetProcAddress(hModule, "free");
    }
    /* Fallback to the CRT malloc/free if we couldn't get them */
    if (!real_malloc) real_malloc = malloc;
    if (!real_free) real_free = free;
#else
    /* On Linux, use dlsym to find the next malloc/free */
    real_malloc = (void* (*)(size_t))dlsym(RTLD_NEXT, "malloc");
    real_free = (void (*)(void*))dlsym(RTLD_NEXT, "free");
    /* If RTLD_NEXT fails, fall back to the standard malloc/free */
    if (!real_malloc) real_malloc = malloc;
    if (!real_free) real_free = free;
#endif

    initialized = 1;
}

/* Thread-local storage for tracking state */
#if defined(_WIN32)
    static __declspec(thread) int in_tracker = 0;
    static __declspec(thread) int is_audio_thread = 0;
#else
    static __thread int in_tracker = 0;
    static __thread int is_audio_thread = 0;
#endif

/* Global tracking flag */
static int tracking_enabled = 0;

/* Violation list structure */
typedef struct violation_node {
    allocation_violation_t violation;
    struct violation_node* next;
} violation_node_t;

/* Head of the violation list */
static violation_node_t* violations_head = NULL;

/* Mutex for the violation list (if needed) */
/* We assume only the audio thread will modify the list while tracking is active,
   but we use a simple critical section for safety on Windows and a mutex on Linux. */
#if defined(_WIN32)
    static CRITICAL_SECTION violations_mutex;
#else
    #include <pthread.h>
    static pthread_mutex_t violations_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/* Initialize the mutex */
static void init_mutex(void) {
    static int initialized = 0;
    if (initialized) return;
#if defined(_WIN32)
    InitializeCriticalSection(&violations_mutex);
#else
    pthread_mutex_init(&violations_mutex, NULL);
#endif
    initialized = 1;
}

/* Lock the mutex */
static void lock_mutex(void) {
    init_mutex();
#if defined(_WIN32)
    EnterCriticalSection(&violations_mutex);
#else
    pthread_mutex_lock(&violations_mutex);
#endif
}

/* Unlock the mutex */
static void unlock_mutex(void) {
#if defined(_WIN32)
    LeaveCriticalSection(&violations_mutex);
#else
    pthread_mutex_unlock(&violations_mutex);
#endif
}

/* Capture a stack trace (platform-specific) */
static void** capture_stack_trace(int* depth) {
    void** buffer = NULL;
    int buffer_size = 128; /* Max stack frames to capture */

    buffer = (void**)real_malloc(buffer_size * sizeof(void*));
    if (!buffer) return NULL;

#if defined(_WIN32)
    *depth = CaptureStackBackTrace(0, buffer_size, buffer, NULL);
#else
    *depth = backtrace(buffer, buffer_size);
#endif

    /* If we captured more than we allocated, realloc */
    if (*depth > buffer_size) {
        void** new_buffer = (void**)real_realloc(buffer, *depth * sizeof(void*));
        if (!new_buffer) {
            real_free(buffer);
            return NULL;
        }
        buffer = new_buffer;
#if defined(_WIN32)
        *depth = CaptureStackBackTrace(0, *depth, buffer, NULL);
#else
        *depth = backtrace(buffer, *depth);
#endif
    }

    return buffer;
}

/* Record an allocation violation */
static void record_allocation(void* ptr, size_t size) {
    if (!tracking_enabled || !is_audio_thread) return;

    lock_mutex();

    violation_node_t* node = (violation_node_t*)real_malloc(sizeof(violation_node_t));
    if (!node) {
        unlock_mutex();
        return;
    }

    node->violation.ptr = ptr;
    node->violation.size = size;
    node->violation.stack_trace = capture_stack_trace(&node->violation.stack_depth);
    node->violation.stack_trace = node->violation.stack_trace; /* Just to suppress warning */

    node->next = violations_head;
    violations_head = node;

    unlock_mutex();
}

/* Tracked malloc implementation */
void* tracked_malloc(size_t size) {
    if (!real_malloc) init_real_functions();

    /* Avoid recursive calls into our tracker */
    if (in_tracker) {
        return real_malloc(size);
    }

    in_tracker = 1;
    void* ptr = real_malloc(size);
    record_allocation(ptr, size);
    in_tracker = 0;

    return ptr;
}

/* Tracked free implementation */
void tracked_free(void* ptr) {
    if (!real_free) init_real_functions();

    /* Avoid recursive calls */
    if (in_tracker) {
        if (real_free) real_free(ptr);
        return;
    }

    in_tracker = 1;
    if (real_free) real_free(ptr);
    in_tracker = 0;
}

/* Tracker control functions */
void tracker_start(void) {
    tracking_enabled = 1;
}

void tracker_stop(void) {
    tracking_enabled = 0;
}

void tracker_mark_audio_thread(void) {
    is_audio_thread = 1;
}

void tracker_unmark_audio_thread(void) {
    is_audio_thread = 0;
}

/* Get violations and free them */
allocation_violation_t* tracker_get_violations(int* count) {
    lock_mutex();

    /* Count the violations */
    int total = 0;
    violation_node_t* current = violations_head;
    while (current) {
        total++;
        current = current->next;
    }

    if (total == 0) {
        unlock_mutex();
        *count = 0;
        return NULL;
    }

    /* Allocate an array for the violations */
    allocation_violation_t* array = (allocation_violation_t*)real_malloc(total * sizeof(allocation_violation_t));
    if (!array) {
        unlock_mutex();
        *count = 0;
        return NULL;
    }

    /* Copy the violations into the array */
    int i = 0;
    current = violations_head;
    while (current) {
        array[i] = current->violation;
        /* We will free the stack trace later when freeing the array */
        i++;
        current = current->next;
    }

    /* Free the linked list */
    current = violations_head;
    while (current) {
        violation_node_t* next = current->next;
        real_free(current->violation.stack_trace);
        real_free(current);
        current = next;
    }
    violations_head = NULL;

    unlock_mutex();

    *count = total;
    return array;
}

void tracker_free_violations(allocation_violation_t* violations, int count) {
    if (!violations || count <= 0) return;
    
    for (int i = 0; i < count; i++) {
        real_free(violations[i].stack_trace);
    }
    real_free(violations);
}

/* We need to fix the header and then the source. Let's do that in the next steps. */
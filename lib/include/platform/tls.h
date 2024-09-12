#ifndef PLZJ_PLATFORM_TLS_H
#define PLZJ_PLATFORM_TLS_H 1

#if defined HAVE_THREADS_H || (defined __has_include && __has_include(<threads.h>))
  #include <threads.h>
#elif !defined __cplusplus && !defined thread_local && \
    (!defined __STDC_VERSION__ || __STDC_VERSION__ <= 201710L)
  /**
   * thread_local macro
   *
   * C++11 / C23 and above already have thread_local keyword
   */
  #if defined _MSC_VER
    /* MSVC are known to break everything, they go first */
    #define thread_local __declspec(thread)
  #elif defined _Thread_local || (defined __STDC_VERSION__ && \
      __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__))
    /**
     * threads are optional in C11 / C17, _Thread_local present in this
     * condition
     */
    #define thread_local _Thread_local
  #else
    /**
     * use __thread bindly so that use of thread_local would emit an error if
     * __thread is undefined
     */
    #define thread_local __thread
  #endif
#endif

#endif /* PLZJ_PLATFORM_TLS_H */

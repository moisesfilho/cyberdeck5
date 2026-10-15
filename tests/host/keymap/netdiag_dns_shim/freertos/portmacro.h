#pragma once
#include <mutex>

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
inline std::mutex &netdiag_test_critical_mutex() {
    static std::mutex mutex;
    return mutex;
}
#define portENTER_CRITICAL(mux) do { (void)(mux); netdiag_test_critical_mutex().lock(); } while (0)
#define portEXIT_CRITICAL(mux) do { (void)(mux); netdiag_test_critical_mutex().unlock(); } while (0)

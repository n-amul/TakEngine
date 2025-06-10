#include "clock.h"
#include <windows.h>

static f64 get_current_time_seconds() {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<f64>(counter.QuadPart) / static_cast<f64>(frequency.QuadPart);
}

void clock::clock_update() {
    if (m_start_time) {
        m_elapsed = get_current_time_seconds() - m_start_time;
    }
}

void clock::clock_start() {
    m_start_time = get_current_time_seconds();
    m_elapsed = 0;
}

void clock::clock_stop() {
    m_start_time = 0; 
}

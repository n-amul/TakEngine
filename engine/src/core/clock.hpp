#pragma once

#include "defines.hpp"

class TAK_API clock {
 public:
  clock() {}
  ~clock() {}

 public:
  f64 m_start_time;
  f64 m_elapsed;

  void clock_update();  // Updates the provided clock. Should be called just
                        // before checking elapsed time. Has no effect on
                        // non-started clocks.
  void clock_start();   // Starts the provided clock. Resets elapsed time.
  void clock_stop();  // Stops the provided clock. Does not reset elapsed time.
};
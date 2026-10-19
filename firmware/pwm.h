#ifndef PWM_H
#define PWM_H
#include <stdint.h>
#include "board.h"

/* Convert the existing 0..1023 logical duty to Timer2 duty counts. Static
   0%/100% endpoints are handled by hw_render. The small-period path fits a
   single 16-bit multiplication; larger periods split it to avoid overflow. */
static inline uint16_t pwm_duty_counts(uint16_t duty)
{
#if BOARD_PWM_PR2 <= 63u
    return (uint16_t)((duty * (BOARD_PWM_PR2 + 1u) + 128u) >> 8);
#else
    return (uint16_t)((duty >> 8) * (BOARD_PWM_PR2 + 1u) +
                      (((duty & 255u) * (BOARD_PWM_PR2 + 1u) + 128u) >> 8));
#endif
}
#endif

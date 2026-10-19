#ifndef PWM_H
#define PWM_H
#include <stdint.h>
#include "board.h"

/* Convert the existing 0..1023 logical duty to Timer2 duty counts. Static
   0%/100% endpoints are handled by hw_render. Split the multiplication so
   all intermediates fit 16 bits, even with PR2=255. */
static inline uint16_t pwm_duty_counts(uint16_t duty)
{
    return (uint16_t)((duty >> 8) * (BOARD_PWM_PR2 + 1u) +
                      (((duty & 255u) * (BOARD_PWM_PR2 + 1u) + 128u) >> 8));
}
#endif

#pragma once
void pm_input_key(unsigned key,int pressed);
void pm_input_clear(void);

#include <stdint.h>
void pm_input_controller(uint64_t buttons,int32_t lx,int32_t ly,int32_t rx,int32_t ry);

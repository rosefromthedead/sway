#ifndef _SWAY_TIMER_FRAME_SCHEDULER_H
#define _SWAY_TIMER_FRAME_SCHEDULER_H
#include <wlr/types/wlr_output.h>

struct sway_timer_frame_scheduler;

struct wlr_frame_scheduler *timed_frame_scheduler_create(struct wlr_output *output,
		int max_render_time);

#endif

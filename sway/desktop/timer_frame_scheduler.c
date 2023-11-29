#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_frame_scheduler.h>
#include <wlr/types/wlr_frame_scheduler.h>
#include <wlr/types/wlr_output.h>
#include "sway/output.h"

struct sway_timer_frame_scheduler {
	struct wlr_frame_scheduler base;

	int max_render_time;

	struct wl_event_source *idle;
	struct wl_event_source *timer;

	bool frame_pending;
	bool needs_frame;

	struct wl_listener commit;
	struct wl_listener present;
};

static void timed_frame_scheduler_destroy(struct wlr_frame_scheduler *wlr_scheduler) {
	struct sway_timer_frame_scheduler *scheduler =
		wl_container_of(wlr_scheduler, scheduler, base);
	if (scheduler->idle != NULL) {
		wl_event_source_remove(scheduler->idle);
	}
	wl_event_source_remove(scheduler->timer);
	wl_list_remove(&scheduler->present.link);
	free(scheduler);
}

static void timed_frame_scheduler_trigger_frame(
		struct sway_timer_frame_scheduler *scheduler) {
	if (!scheduler->needs_frame) {
		return;
	}
	scheduler->needs_frame = false;
	wl_signal_emit_mutable(&scheduler->base.events.frame, NULL);
}

static void timed_frame_scheduler_handle_idle(void *data) {
	struct sway_timer_frame_scheduler *scheduler = data;
	scheduler->idle = NULL;
	timed_frame_scheduler_trigger_frame(scheduler);
}

static void timed_frame_scheduler_schedule_frame(struct wlr_frame_scheduler *wlr_scheduler) {
	struct sway_timer_frame_scheduler *scheduler =
		wl_container_of(wlr_scheduler, scheduler, base);
	scheduler->needs_frame = true;
	if (scheduler->idle != NULL || scheduler->frame_pending) {
		return;
	}
	scheduler->idle = wl_event_loop_add_idle(scheduler->base.output->event_loop,
		timed_frame_scheduler_handle_idle, scheduler);
}

static const struct wlr_frame_scheduler_impl timed_frame_scheduler_impl = {
	.destroy = timed_frame_scheduler_destroy,
	.schedule_frame = timed_frame_scheduler_schedule_frame,
};

static int timed_frame_scheduler_handle_timer(void *data) {
	struct sway_timer_frame_scheduler *scheduler = data;
	scheduler->frame_pending = false;
	timed_frame_scheduler_trigger_frame(scheduler);
	return 0;
}

static void timed_frame_scheduler_handle_present(struct wl_listener *listener, void *data) {
	struct sway_timer_frame_scheduler *scheduler =
		wl_container_of(listener, scheduler, present);
	const struct wlr_output_event_present *event = data;

	if (!event->presented) {
		return;
	}

	assert(!scheduler->frame_pending);

	if (scheduler->idle != NULL) {
		wl_event_source_remove(scheduler->idle);
		scheduler->idle = NULL;
	}

	int msec_until_refresh = get_msec_until_refresh(event);
	int delay = msec_until_refresh - scheduler->max_render_time;

	// If the delay is less than 1 millisecond (which is the least we can wait)
	// then just render right away.
	if (delay < 1) {
		wl_signal_emit_mutable(&scheduler->base.events.frame, NULL);
	} else {
		scheduler->frame_pending = true;
		wl_event_source_timer_update(scheduler->timer, delay);
	}
}

struct wlr_frame_scheduler *timed_frame_scheduler_create(struct wlr_output *output,
		int max_render_time) {
	struct sway_timer_frame_scheduler *scheduler = calloc(1, sizeof(*scheduler));
	if (scheduler == NULL) {
		return NULL;
	}
	wlr_frame_scheduler_init(&scheduler->base, &timed_frame_scheduler_impl, output);

	scheduler->max_render_time = max_render_time;

	scheduler->timer = wl_event_loop_add_timer(server.wl_event_loop,
		timed_frame_scheduler_handle_timer, scheduler);

	scheduler->present.notify = timed_frame_scheduler_handle_present;
	wl_signal_add(&output->events.present, &scheduler->present);

	return &scheduler->base;
}

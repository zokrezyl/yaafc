/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `time`.
 * NEVER include this from outside src/yaafc/plugins/time/. */
#ifndef YAAFC_TIME_INTERNAL_H
#define YAAFC_TIME_INTERNAL_H

#include <yaafc/plugin/time/time.h>

typedef struct yaafc_int64_result (*time_clock_now_ms_fn)(struct ctx *, struct object *);
typedef struct yaafc_int64_result (*time_clock_sleep_ms_fn)(struct ctx *, struct object *, uint32_t);

#endif

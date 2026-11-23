/* Age of Calamity (01002B00111A2000) bridge between KTGL companion state and host hooks. */
#pragma once
#include "ktgl_companion.h"

int hwaoc_companion_state(CompanionState *out, int64_t *age_ms);
void hwaoc_companion_page(int page);
int hwaoc_companion_detail(int32_t *ints, int max_ints, float *flts, int max_flts, char *txt, int max_txt);
int hwaoc_companion_fire(int action_id);
void hwaoc_companion_abort(void);
int hwaoc_companion_pin(int op, int slot, const char *label);

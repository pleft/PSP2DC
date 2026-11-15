/* See card_sync.h. */
#include "card_sync.h"

SyncChoice card_sync_choose(const SyncState *s)
{
    if (!s->have_card) return SYNC_ADOPT;
    if (s->have_ours && s->ours == s->a) return s->a_is_mine ? SYNC_SAME : SYNC_PUT_OURS;
    if (s->have_ours && s->base_is_a && s->ours == s->base) return SYNC_TAKE;
    if (s->have_ours && s->base_is_a && s->a == s->base) return SYNC_PUT_OURS;
    return SYNC_KEEP_BOTH;
}

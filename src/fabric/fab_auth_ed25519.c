/* fab_auth_ed25519.c -- Ed25519 FabAuth over the vendored AIENOS signing code. */
#include "fab_auth_ed25519.h"
#include "searchtrace/sig/aienos_sig.h"

#include <string.h>

int fab_ed_machine_id(const uint8_t pk[32], AienMachineId *out) {
    if (!pk || !out) return FAB_E_ARG;
    return aien_mid_derive(AIEN_MID_ROOT_PROVISIONED, pk, 32, out) == AIEN_MID_OK ? FAB_OK : FAB_E_ARG;
}

const uint8_t *fab_ed_auth_pk_of(const FabEdAuth *a, const AienMachineId *id) {
    if (!a || !id) return NULL;
    if (aien_mid_equal(&a->self, id)) return a->pk;
    for (uint32_t i = 0; i < a->n; i++)
        if (aien_mid_equal(&a->peer[i].id, id)) return a->peer[i].pk;
    return NULL;
}

static int ed_sign(void *ctx, const uint8_t *msg, size_t len, uint8_t sig[FAB_SIG_BYTES]) {
    const FabEdAuth *a = ctx;
    if (!a || !sig || (!msg && len)) return FAB_E_ARG;
    return aienos_ed25519_sign(sig, msg, len, a->sk) == AIENOS_SIG_OK ? FAB_OK : FAB_E_AUTH;
}

static int ed_verify(void *ctx, const AienMachineId *claimed, const uint8_t *msg, size_t len,
                     const uint8_t sig[FAB_SIG_BYTES]) {
    const FabEdAuth *a = ctx;
    if (!a || !claimed || !sig || (!msg && len)) return FAB_E_ARG;
    const uint8_t *pk = fab_ed_auth_pk_of(a, claimed);
    if (!pk) return FAB_E_AUTH;                   /* not enrolled here: never a silent accept */
    return aienos_ed25519_verify(sig, msg, len, pk) == AIENOS_SIG_OK ? FAB_OK : FAB_E_AUTH;
}

int fab_ed_auth_init(FabEdAuth *a, const uint8_t sk[32]) {
    if (!a || !sk) return FAB_E_ARG;
    memset(a, 0, sizeof *a);
    memcpy(a->sk, sk, 32);
    if (aienos_ed25519_public_key(a->pk, a->sk) != AIENOS_SIG_OK ||
        fab_ed_machine_id(a->pk, &a->self) != FAB_OK) {
        fab_ed_auth_wipe(a);
        return FAB_E_ARG;
    }
    a->auth.ctx = a;
    a->auth.sign = ed_sign;
    a->auth.verify = ed_verify;
    return FAB_OK;
}

int fab_ed_auth_add_peer(FabEdAuth *a, const uint8_t pk[32]) {
    if (!a || !pk) return FAB_E_ARG;
    if (a->n >= FAB_ED_MAX_PEERS) return FAB_E_FULL;
    FabEdPeer p;
    memcpy(p.pk, pk, 32);
    if (fab_ed_machine_id(p.pk, &p.id) != FAB_OK) return FAB_E_ARG;
    if (fab_ed_auth_pk_of(a, &p.id)) return FAB_E_ARG;   /* duplicate, or our own key */
    a->peer[a->n++] = p;
    return FAB_OK;
}

int fab_ed_auth_roster(const FabEdAuth *a, AienMachineId *ids, uint32_t cap, uint32_t *n) {
    if (!a || !ids || !n) return FAB_E_ARG;
    if (cap < a->n + 1u) return FAB_E_FULL;
    ids[0] = a->self;
    for (uint32_t i = 0; i < a->n; i++) ids[i + 1] = a->peer[i].id;
    *n = a->n + 1u;
    return FAB_OK;
}

void fab_ed_auth_wipe(FabEdAuth *a) {
    if (!a) return;
    volatile uint8_t *p = (volatile uint8_t *)a;
    for (size_t i = 0; i < sizeof *a; i++) p[i] = 0;
}

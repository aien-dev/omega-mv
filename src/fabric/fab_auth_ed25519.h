/*
 * fab_auth_ed25519.h -- the Fabric signer for real machines (aienix#6, omega#536).
 *
 * FabAuth (fabric.h) is an interface: fabric.c signs every message it seals and
 * verifies every message it receives through it, naming the claimed sender by
 * its AienMachineId. F5-0 ships only an HMAC stand-in (fab_hmac.h, test build).
 * This file is the production signer: Ed25519 over the exact AFAB bytes, with
 * the signing code the repository already vendors from AIENOS
 * (src/searchtrace/sig/, aienos native/sig @913b962; see PROVENANCE.txt there).
 * No new cryptography is written here.
 *
 * Identity: one 32-byte Ed25519 seed per machine (RFC 8032 secret key). The
 * machine's canonical identity is derived from its public key,
 *   AienMachineId = aien_mid_derive(AIEN_MID_ROOT_PROVISIONED, pk, 32),
 * so a roster entry is the public key alone; the id is never typed by hand.
 * Class: lab-provisioned. This is not TRUST-1 owner enrolment (AIEN_MID_ROOT_
 * HARDWARE); a receipt made under such a key must say identity_class=lab.
 *
 * verify(claimed, msg, sig): the claimed id must be this node's own id or one
 * of the enrolled peers; its public key verifies the signature. Anything else
 * is a refusal (nonzero). fabric.c has already checked the roster before it
 * asks, so an unknown id here is FAB_E_AUTH, never a silent accept.
 *
 * The seed stays in this struct only; fab_ed_auth_wipe zeroes it. Nothing here
 * reads a file, opens a socket or prints: the caller loads the seed bytes.
 */
#ifndef FAB_AUTH_ED25519_H
#define FAB_AUTH_ED25519_H

#include "fabric.h"

#define FAB_ED_MAX_PEERS 64u

typedef struct {
    AienMachineId id;
    uint8_t pk[32];
} FabEdPeer;

typedef struct {
    uint8_t sk[32];                 /* this machine's seed; wiped by fab_ed_auth_wipe */
    uint8_t pk[32];
    AienMachineId self;
    uint32_t n;
    FabEdPeer peer[FAB_ED_MAX_PEERS];
    FabAuth auth;                   /* points back at this signer */
} FabEdAuth;

/* The canonical machine id of a public key. FAB_E_ARG on a null pointer. */
int fab_ed_machine_id(const uint8_t pk[32], AienMachineId *out);

/* Loads the seed, derives pk and self. The peer list starts empty. */
int fab_ed_auth_init(FabEdAuth *a, const uint8_t sk[32]);

/* Enrols a peer by public key (its id is derived). FAB_E_FULL when the list
 * is full, FAB_E_ARG for a duplicate or for this machine's own key. */
int fab_ed_auth_add_peer(FabEdAuth *a, const uint8_t pk[32]);

/* Writes self + every peer into ids (the FabRoster a node is configured with).
 * FAB_E_FULL when cap is too small; *n is the count written. */
int fab_ed_auth_roster(const FabEdAuth *a, AienMachineId *ids, uint32_t cap, uint32_t *n);

/* The public key enrolled for `id`, or NULL. */
const uint8_t *fab_ed_auth_pk_of(const FabEdAuth *a, const AienMachineId *id);

/* Zeroes the seed and the struct. */
void fab_ed_auth_wipe(FabEdAuth *a);

#endif /* FAB_AUTH_ED25519_H */

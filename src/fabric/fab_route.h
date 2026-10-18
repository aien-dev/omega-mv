/*
 * fab_route.h -- the checks a Fabric node makes before it sends work to a
 * remote machine (aienix#6, omega#536). Production-buildable.
 *
 * These are steps 1 to 3 of fab_dispatch.h, lifted out of the test-only
 * in-process dispatcher so a real coordinator can run them before it hands a
 * task to the transport. Step 4 of the dispatcher (the simulated procedure
 * table) is NOT here and is never promoted: whether the remote machine ran
 * what it advertised is answered by the executor's receipt (fab_work.h HTR1,
 * implementation_digest against the advertised skill_digest), not by this
 * process.
 *
 *   1. fab_route_pump: apply waiting Fabric traffic and the clock, so a LEAVE
 *      or an ended lease withdraws the machine's entries first;
 *   2. the route must still be SR_E_REMOTE under sr_route_check (entry live,
 *      same generation, version and digest, machine identity unchanged);
 *   3. the route's machine must be a live member of this node whose home is
 *      JS_HOME_REMOTE_OWNED: never the local machine, never a lost one.
 * Any failure refuses and names why. The caller holds whatever lock protects
 * the node; this file takes none (fab_dispatch.c keeps its own).
 */
#ifndef FAB_ROUTE_H
#define FAB_ROUTE_H

#include "fabric.h"
#include "runtime/rx_skillroute.h"

enum {
    FAB_RT_OK = 0,
    FAB_RT_ARG,              /* bad call, or the router's graph is not this node's catalog */
    FAB_RT_TRANSPORT,        /* applying waiting Fabric traffic failed */
    FAB_RT_ROUTE,            /* sr_route_check is no longer SR_E_REMOTE */
    FAB_RT_TARGET,           /* route has no canonical target machine */
    FAB_RT_NOT_MEMBER,       /* target is not a live remote member of this node */
    FAB_RT_N
};

/* Step 1. Applies every waiting message, then the clock. Returns the number of
 * messages applied, or <0 (a FAB_E_* transport code). polled / refused_msgs,
 * when given, are increased by the messages seen / refused. */
int fab_route_pump(FabNode *n, uint64_t now_us, uint64_t *polled, uint64_t *refused_msgs);

/* Steps 1 to 3. FAB_RT_OK with *target = the live remote machine to send to,
 * else the FAB_RT_* refusal (target untouched). */
int fab_route_check(FabNode *n, const SrRouter *router, const SrRoute *route, uint64_t now_us,
                    AienMachineId *target, uint64_t *polled, uint64_t *refused_msgs);

const char *fab_route_strerror(int code);

#endif /* FAB_ROUTE_H */

/* fab_route.c -- pre-dispatch route checks (fab_route.h). */
#include "fab_route.h"

int fab_route_pump(FabNode *n, uint64_t now_us, uint64_t *polled, uint64_t *refused_msgs) {
    if (!n) return FAB_E_ARG;
    int applied = 0;
    FabVerdict v;
    for (;;) {
        int rc = fab_poll(n, now_us, &v);
        if (rc < 0) return rc;
        if (rc == 0) break;
        applied++;
        if (polled) (*polled)++;
        if (v.code != FAB_OK && refused_msgs) (*refused_msgs)++;
    }
    (void)fab_tick(n, now_us);
    return applied;
}

int fab_route_check(FabNode *n, const SrRouter *router, const SrRoute *route, uint64_t now_us,
                    AienMachineId *target, uint64_t *polled, uint64_t *refused_msgs) {
    if (!n || !router || !route || !target || router->graph != n->cfg.catalog) return FAB_RT_ARG;
    if (fab_route_pump(n, now_us, polled, refused_msgs) < 0) return FAB_RT_TRANSPORT;
    if (sr_route_check(router, route, now_us) != SR_E_REMOTE) return FAB_RT_ROUTE;
    if (!route->target_known) return FAB_RT_TARGET;
    JsHome home;
    if (fab_home(n, &route->target, now_us, &home) != FAB_OK || home.locality != JS_HOME_REMOTE_OWNED)
        return FAB_RT_NOT_MEMBER;
    *target = route->target;
    return FAB_RT_OK;
}

const char *fab_route_strerror(int code) {
    switch (code) {
    case FAB_RT_OK: return "ok";
    case FAB_RT_ARG: return "bad call or foreign graph";
    case FAB_RT_TRANSPORT: return "fabric transport error while pumping";
    case FAB_RT_ROUTE: return "route no longer remote (withdrawn, stale or lease ended)";
    case FAB_RT_TARGET: return "route has no canonical target machine";
    case FAB_RT_NOT_MEMBER: return "target is not a live remote member";
    default: return "unknown fab_route code";
    }
}

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "pmu_contract.h"

int main(void) {
    unsigned long long code = 0;
    unsigned long long midr = 0;
    int pmu_type = 0;
    struct pmu_trial_eval_out ev;
    char json[256];

    /* 1. Event code discovery and negative controls */
    assert(pmu_event_code("event=0x0021\n", 0x21, &code) && code == 0x21);
    assert(!pmu_event_code("event=0x10\n", 0x21, &code));
    assert(!pmu_event_code("event=0x10\n", 0x22, &code)); /* speculative br_mis_pred != br_mis_pred_retired */
    assert(!pmu_event_code("event=0x21,junk=1", 0x21, &code));
    assert(!pmu_event_code("event=-1", 0x21, &code));
    assert(!pmu_event_code("", 0x21, &code));
    assert(!pmu_event_code(NULL, 0x21, &code));
    assert(!pmu_event_code("event=0x0021\n", 0x21, NULL));
    assert(!pmu_event_code("config=0x0021\n", 0x21, &code));
    assert(!pmu_event_code("other=0x0021\n", 0x21, &code));
    assert(!pmu_event_code("event=0x\n", 0x21, &code));
    assert(!pmu_event_code("event=0x 21\n", 0x21, &code));
    assert(!pmu_event_code("event=0x+21\n", 0x21, &code));
    assert(!pmu_event_code("event=0xfffffffffffffffff\n", ULLONG_MAX, &code));

    /* 2. CPU list membership and malformed list controls */
    assert(pmu_cpu_member("0-4,10-14\n", 1) == 1);
    assert(pmu_cpu_member("0-4,10-14\n", 6) == 0);
    assert(pmu_cpu_member("5-9,15-19\n", 6) == 1);
    assert(pmu_cpu_member("5-9,15-19\n", 1) == 0);
    assert(pmu_cpu_member("0-4,broken", 1) == -1);
    assert(pmu_cpu_member("0-4junk", 1) == -1);
    assert(pmu_cpu_member("0-4 5", 1) == -1);
    assert(pmu_cpu_member(" 0-4\n", 1) == -1);
    assert(pmu_cpu_member("0-+4\n", 1) == -1);
    assert(pmu_cpu_member("4-0", 1) == -1);
    assert(pmu_cpu_member("0,", 0) == -1);
    assert(pmu_cpu_member("0,\n", 0) == -1);
    assert(pmu_cpu_member("0\n1", 0) == -1);
    assert(pmu_cpu_member("0-", 0) == -1);
    assert(pmu_cpu_member("-1", 0) == -1);
    assert(pmu_cpu_member("", 0) == -1);
    assert(pmu_cpu_member("\n", 0) == -1);
    assert(pmu_cpu_member(NULL, 0) == -1);
    assert(pmu_cpu_member("0-4", -1) == -1);
    assert(pmu_cpu_member("999999999999999999999999", 0) == -1);
    assert(pmu_cpu_member("0-999999999999999999999999", 0) == -1);

    /* 3. Core MIDR and PMU type controls */
    assert(pmu_core(0x410fd871ULL) == 725);
    assert(pmu_core(0x410fd851ULL) == 925);
    assert(pmu_core(0x420fd871ULL) == 0);
    assert(pmu_core(0x410fd031ULL) == 0);
    assert(pmu_parse_midr("0x00000000410fd871\n", &midr) && pmu_core(midr) == 725);
    assert(pmu_parse_midr("0x00000000410fd851\n", &midr) && pmu_core(midr) == 925);
    assert(!pmu_parse_midr(NULL, &midr));
    assert(!pmu_parse_midr("410fd871\n", &midr));
    assert(!pmu_parse_midr("0x\n", &midr));
    assert(!pmu_parse_midr("0x 410fd871\n", &midr));
    assert(!pmu_parse_midr("0x410fd871junk", &midr));
    assert(pmu_parse_type("10\n", 10, &pmu_type) && pmu_type == 10);
    assert(pmu_parse_type("11\n", 11, &pmu_type) && pmu_type == 11);
    assert(!pmu_parse_type("10\n", 11, &pmu_type));
    assert(!pmu_parse_type(NULL, 10, &pmu_type));
    assert(!pmu_parse_type("abc\n", 10, &pmu_type));
    assert(!pmu_parse_type("10junk", 10, &pmu_type));

    /* 4. Status classification negative controls */
    assert(!strcmp(pmu_status(EACCES, 0, 0, 0, 0, 0), "PERMISSION_DENIED"));
    assert(!strcmp(pmu_status(EPERM, 0, 0, 0, 0, 0), "PERMISSION_DENIED"));
    assert(!strcmp(pmu_status(EINVAL, 0, 0, 0, 0, 0), "UNSUPPORTED_EVENT"));
    assert(!strcmp(pmu_status(ENOENT, 0, 0, 0, 0, 0), "UNSUPPORTED_EVENT"));
    assert(!strcmp(pmu_status(EBUSY, 0, 24, 100, 100, 0), "COUNTER_UNAVAILABLE"));
    assert(!strcmp(pmu_status(0, 0, 24, 100, 100, 0), "OK"));
    assert(!strcmp(pmu_status(0, 0, 24, 100, 50, 0), "MULTIPLEXED"));
    assert(!strcmp(pmu_status(0, 0, 24, 100, 50, 1), "RUNNING_TIME_INCOMPLETE"));
    assert(strcmp(pmu_status(0, 0, 24, 100, 0, 0), "OK"));
    assert(strcmp(pmu_status(0, 0, 24, 0, 0, 0), "OK"));
    assert(strcmp(pmu_status(0, 0, 24, 10, 11, 0), "OK"));
    assert(strcmp(pmu_status(0, 0, 8, 100, 100, 0), "OK"));
    assert(strcmp(pmu_status(0, EIO, 24, 100, 100, 0), "OK"));

    /* 5. Trial evaluation & JSON null-count enforcement on all non-OK paths */
    /* Failed pin */
    pmu_eval_trial(EINVAL, 0, 0, 0, 24, 999, 100, 100, 0, &ev);
    assert(!strcmp(ev.status, "COUNTER_UNAVAILABLE") && !ev.has_raw_count);
    assert(pmu_format_trial_counts(json, sizeof(json), &ev) > 0);
    assert(strstr(json, "\"raw_count\":null") != NULL);

    /* Failed ioctl */
    pmu_eval_trial(0, 0, EBADF, 0, 24, 999, 100, 100, 0, &ev);
    assert(!strcmp(ev.status, "COUNTER_UNAVAILABLE") && !ev.has_raw_count);
    assert(pmu_format_trial_counts(json, sizeof(json), &ev) > 0);
    assert(strstr(json, "\"raw_count\":null") != NULL);

    /* Denied open */
    pmu_eval_trial(0, EACCES, 0, 0, 0, 0, 0, 0, 0, &ev);
    assert(!strcmp(ev.status, "PERMISSION_DENIED") && !ev.has_raw_count);
    assert(pmu_format_trial_counts(json, sizeof(json), &ev) > 0);
    assert(strstr(json, "\"status\":\"PERMISSION_DENIED\",\"raw_count\":null") != NULL);

    /* Multiplexed: raw_count MUST still be null (no scaled or partial counts) */
    pmu_eval_trial(0, 0, 0, 0, 24, 50000, 1000, 500, 0, &ev);
    assert(!strcmp(ev.status, "MULTIPLEXED") && !ev.has_raw_count && ev.has_times);
    assert(pmu_format_trial_counts(json, sizeof(json), &ev) > 0);
    assert(strstr(json, "\"status\":\"MULTIPLEXED\",\"raw_count\":null,\"time_enabled_ns\":1000,\"time_running_ns\":500") != NULL);

    /* External / cross-core incomplete running time: raw_count MUST be null */
    pmu_eval_trial(0, 0, 0, 0, 24, 50000, 1000, 500, 1, &ev);
    assert(!strcmp(ev.status, "RUNNING_TIME_INCOMPLETE") && !ev.has_raw_count && ev.has_times);
    assert(pmu_format_trial_counts(json, sizeof(json), &ev) > 0);
    assert(strstr(json, "\"status\":\"RUNNING_TIME_INCOMPLETE\",\"raw_count\":null") != NULL);

    /* Valid OK trial */
    pmu_eval_trial(0, 0, 0, 0, 24, 424242, 1000, 1000, 0, &ev);
    assert(!strcmp(ev.status, "OK") && ev.has_raw_count && ev.raw_count == 424242);
    assert(pmu_format_trial_counts(json, sizeof(json), &ev) > 0);
    assert(strstr(json, "\"status\":\"OK\",\"raw_count\":424242,\"time_enabled_ns\":1000,\"time_running_ns\":1000") != NULL);

    /* 6. Event catalog parity check */
    assert(pmu_find_event("cpu_cycles") == 0 && pmu_events[0].code == 0x11 && pmu_events[0].legacy == 1);
    assert(pmu_find_event("inst_retired") == 1 && pmu_events[1].code == 0x08 && pmu_events[1].legacy == 1);
    assert(pmu_find_event("bus_access") == 2 && pmu_events[2].code == 0x19 && pmu_events[2].legacy == 1);
    assert(pmu_find_event("ll_cache_miss_rd") == 3 && pmu_events[3].code == 0x37 && pmu_events[3].legacy == 1);
    assert(pmu_find_event("l2d_cache_refill") == 4 && pmu_events[4].code == 0x17 && pmu_events[4].legacy == 1);
    assert(pmu_find_event("mem_access") == 5 && pmu_events[5].code == 0x13 && pmu_events[5].legacy == 1);
    assert(pmu_find_event("br_retired") == 6 && pmu_events[6].code == 0x21 && pmu_events[6].legacy == 0);
    assert(pmu_find_event("br_mis_pred_retired") == 7 && pmu_events[7].code == 0x22 && pmu_events[7].legacy == 0);
    assert(pmu_find_event("stall_frontend") == 8 && pmu_events[8].code == 0x23 && pmu_events[8].legacy == 0);
    assert(pmu_find_event("stall_backend") == 9 && pmu_events[9].code == 0x24 && pmu_events[9].legacy == 0);
    assert(pmu_find_event("br_mis_pred") == -1);

    puts("contract negative controls PASS");
    return 0;
}

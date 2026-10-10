#include "scxml_component_interceptor_probe.h"

#include <salts/clock.h>
#include <salts/plugin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pure native-call overhead microbenchmark, deliberately separate from the
 * W3C/SCXML semantic gate. Both measurements use the SAME loaded real DSO,
 * Component Scope and move-only ticket commit contract. No network/Statechart
 * is measured here; those are separately qualified by the runtime tests. */
enum { BENCH_SAMPLES = 7, BENCH_WARMUP = 10000, BENCH_ITERATIONS = 100000 };

typedef struct bench_generation {
    salts_component_plugin_generation generation;
    salts_component_deployment deployments[1];
    salts_component_instance instances[1];
    salts_component_dependency dependencies[1];
    size_t activation_order[1];
    salts_component_plugin_module modules[1];
} bench_generation;

static int by_elapsed_ns(const void *a, const void *b) {
    const uint64_t x = *(const uint64_t *)a;
    const uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static bool bench_batch(
    scxml_intercept_probe *probe,
    const scxml_send_request *request,
    size_t iterations,
    bool intercepted,
    uint64_t *out_ns) {
    const uint64_t start = cmeta_hrtime();
    for (size_t i = 0u; i < iterations; ++i) {
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        scxml_adapter_status status;
        if (intercepted) {
            status = scxml_intercept_prepare_send(
                probe, request, &ticket, &error);
        } else {
            status = probe->provider->prepare_send(
                probe->provider_user, request, &ticket, &error);
        }
        if (status != SCXML_ADAPTER_ACCEPTED || error != NULL ||
            ticket.commit == NULL || ticket.discard == NULL)
            return false;
        /* Real DSO callback: prevent dead-code elimination, match the same
           ticket settlement and marker mutation in both call paths. */
        ticket.commit(ticket.user);
    }
    if (out_ns != NULL) *out_ns = cmeta_hrtime() - start;
    return true;
}

static int active_dso_marker(const scxml_component_scope *scope) {
    salts_component_service service = {0};
    if (salts_component_plugin_scope_find_service_from(
            &scope->component_scope, "ScxmlDsoFixture",
            scxml_event_io_provider_interface(), &service) !=
        SALTS_COMPONENT_PLUGIN_OK || service.object == NULL ||
        !cmeta_data_desc_equal(service.object->data, &cmeta_data_int))
        return -1;
    return *(const int *)service.object->object;
}

#define BENCH_REQUIRE(condition) do {                                    \
    if (!(condition)) {                                                  \
        fprintf(stderr, "DSO Interceptor microbenchmark failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #condition);                       \
        return EXIT_FAILURE;                                            \
    }                                                                   \
} while (0)

int main(void) {
    cmeta_plugin_registry registry = {0};
    cmeta_plugin_ref plugin = {0};
    salts_component_plugin_runtime runtime = {0};
    salts_component_plugin_generation *previous = NULL;
    scxml_component_scope scope = {0};
    scxml_component_event_io_provider provider = {0};
    scxml_intercept_probe interceptor = {0};
    bench_generation g = {0};
    const cmeta_plugin_registry_config registry_config = {.capacity = 1u};
    const salts_component_plugin_generation_storage storage = {
        g.deployments, 1u, g.instances, 1u, g.dependencies, 1u,
        g.activation_order, 1u, g.modules, 1u};
    const scxml_send_request request = {.event = "probe", .event_size = 5u};
    const scxml_event_io_adapter *adapter;
    const salts_component_plugin_source *source;
    uint64_t direct[BENCH_SAMPLES] = {0};
    uint64_t through[BENCH_SAMPLES] = {0};
    bool quiescent = false;

    BENCH_REQUIRE(cmeta_plugin_registry_init(
        &registry, &registry_config) == CMETA_PLUGIN_OK);
    BENCH_REQUIRE(cmeta_plugin_registry_load(
        &registry, SCXML_COMPONENT_DSO_ONE, &plugin) == CMETA_PLUGIN_OK);
    BENCH_REQUIRE(cmeta_plugin_registry_start(
        &registry, plugin) == CMETA_PLUGIN_OK);
    {
        const salts_component_plugin_source actual_source = {
            plugin, "component-provider", NULL, NULL
        };
        source = &actual_source;
        BENCH_REQUIRE(salts_component_plugin_generation_build(
            &g.generation, UINT64_C(91), &registry, &storage,
            NULL, 0u, source, 1u, NULL, 0u) == SALTS_COMPONENT_PLUGIN_OK);
    }
    BENCH_REQUIRE(salts_component_plugin_runtime_init(
        &runtime) == SALTS_COMPONENT_PLUGIN_OK);
    BENCH_REQUIRE(salts_component_plugin_runtime_publish(
        &runtime, &g.generation, &previous) == SALTS_COMPONENT_PLUGIN_OK);
    BENCH_REQUIRE(previous == NULL);
    BENCH_REQUIRE(scxml_component_scope_acquire(
        &scope, &runtime) == SCXML_COMPONENT_OK);
    BENCH_REQUIRE(scxml_component_event_io_provider_bind(
        &provider, &scope, "ScxmlDsoFixture",
        SCXML_EVENT_IO_CAP_SEND) == SCXML_COMPONENT_OK);
    adapter = scxml_component_event_io_provider_adapter(&provider);
    BENCH_REQUIRE(adapter != NULL);
    BENCH_REQUIRE(scxml_intercept_probe_init(
        &interceptor, adapter,
        scxml_component_event_io_provider_user(&provider)) == CMETA_OK);
    BENCH_REQUIRE(active_dso_marker(&scope) == 100);

    /* Warm both code paths equally without retaining a Provider ticket.
       Alternate order in every sample to reduce systematic order bias. */
    BENCH_REQUIRE(bench_batch(
        &interceptor, &request, BENCH_WARMUP, false, NULL));
    BENCH_REQUIRE(bench_batch(
        &interceptor, &request, BENCH_WARMUP, true, NULL));
    for (size_t i = 0u; i < BENCH_SAMPLES; ++i) {
        if (i % 2u == 0u) {
            BENCH_REQUIRE(bench_batch(
                &interceptor, &request, BENCH_ITERATIONS, false, &direct[i]));
            BENCH_REQUIRE(bench_batch(
                &interceptor, &request, BENCH_ITERATIONS, true, &through[i]));
        } else {
            BENCH_REQUIRE(bench_batch(
                &interceptor, &request, BENCH_ITERATIONS, true, &through[i]));
            BENCH_REQUIRE(bench_batch(
                &interceptor, &request, BENCH_ITERATIONS, false, &direct[i]));
        }
        printf("sample=%zu direct_ns=%llu interceptor_ns=%llu operations=%u\n",
               i + 1u,
               (unsigned long long)direct[i],
               (unsigned long long)through[i],
               (unsigned)BENCH_ITERATIONS);
    }
    BENCH_REQUIRE(interceptor.target_calls ==
                  BENCH_WARMUP + BENCH_SAMPLES * BENCH_ITERATIONS);
    BENCH_REQUIRE(interceptor.discarded_on_failure == 0u);
    BENCH_REQUIRE(active_dso_marker(&scope) ==
        100 + 2 * BENCH_WARMUP + 2 * BENCH_SAMPLES * BENCH_ITERATIONS);

    qsort(direct, BENCH_SAMPLES, sizeof(direct[0]), by_elapsed_ns);
    qsort(through, BENCH_SAMPLES, sizeof(through[0]), by_elapsed_ns);
    BENCH_REQUIRE(direct[BENCH_SAMPLES / 2u] > 0u);
    printf("model=real_DSO_prepare_send_commit samples=%u iterations=%u "
           "warmup=%u hooks=2 direct_median_ns_per_op=%.3f "
           "interceptor_median_ns_per_op=%.3f ratio=%.3f "
           "source=pinned_Component_Scope no_latency_threshold=yes\n",
           (unsigned)BENCH_SAMPLES, (unsigned)BENCH_ITERATIONS,
           (unsigned)BENCH_WARMUP,
           (double)direct[BENCH_SAMPLES / 2u] / (double)BENCH_ITERATIONS,
           (double)through[BENCH_SAMPLES / 2u] / (double)BENCH_ITERATIONS,
           (double)through[BENCH_SAMPLES / 2u] /
                (double)direct[BENCH_SAMPLES / 2u]);

    adapter->close(scxml_component_event_io_provider_user(&provider));
    BENCH_REQUIRE(scxml_component_event_io_provider_destroy(
        &provider) == SCXML_COMPONENT_OK);
    BENCH_REQUIRE(scxml_component_scope_release(&scope) == SCXML_COMPONENT_OK);
    BENCH_REQUIRE(salts_component_plugin_runtime_close(
        &runtime, &previous) == SALTS_COMPONENT_PLUGIN_OK);
    BENCH_REQUIRE(previous == &g.generation);
    BENCH_REQUIRE(salts_component_plugin_generation_drain(
        &runtime, &g.generation) == SALTS_COMPONENT_PLUGIN_OK);
    BENCH_REQUIRE(salts_component_plugin_runtime_destroy(
        &runtime) == SALTS_COMPONENT_PLUGIN_OK);
    BENCH_REQUIRE(cmeta_plugin_registry_request_stop(
        &registry, plugin) == CMETA_PLUGIN_OK);
    BENCH_REQUIRE(cmeta_plugin_registry_poll_quiescent(
        &registry, plugin, &quiescent) == CMETA_PLUGIN_OK);
    BENCH_REQUIRE(quiescent);
    BENCH_REQUIRE(cmeta_plugin_registry_unload(
        &registry, plugin) == CMETA_PLUGIN_OK);
    BENCH_REQUIRE(cmeta_plugin_registry_destroy(&registry) == CMETA_PLUGIN_OK);
    return EXIT_SUCCESS;
}

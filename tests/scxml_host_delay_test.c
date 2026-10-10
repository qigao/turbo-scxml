#include <scxml/host_event_io.h>

#include <cflow/clock.h>
#include <cflow/executor.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

static scxml_session_config timed_session_config(
    scxml_program *program, cflow_executor *executor,
    scxml_host_event_io_binding *binding) {
    scxml_session_config config = {0};
    config.program = program;
    config.executor = executor;
    /* CFlow's Statechart does not own the Host deadline ledger. A second
       Statechart timer queue would duplicate the Host clock authority. */
    config.external_event_capacity = 2u;
    config.internal_event_capacity = 2u;
    config.completion_capacity = 2u;
    config.effect_capacity = 4u;
    config.adapter_internal_event_capacity = 2u;
    config.delayed_send_capacity = 2u;
    config.microstep_limit = 24u;
    config.event_io = scxml_host_event_io_binding_delayed_adapter();
    config.adapter_user = scxml_host_event_io_binding_user(binding);
    return config;
}

/* A cancel effect ticket is move-only, but an old ticket may still be
 * executing when the Host reuses the sole delayed-event row. The Host
 * mutex and the row identity (not the sendid or row address alone) decide
 * whether that ticket may consume the current row. */
typedef struct delayed_aba_worker {
    scxml_host_router *router;
    scxml_host_session_ref source;
    atomic_int ready;
    atomic_int fire;
    int release_status;
    int prepare_status;
    size_t cancelled_rows;
} delayed_aba_worker;

static void reuse_delayed_row_from_other_thread(void *user) {
    delayed_aba_worker *race = (delayed_aba_worker *)user;
    cflow_statechart_effect_ticket next = {0};
    const uint64_t deadline = cmeta_monotonic_ms() + UINT64_C(5000);
    race->release_status = SALTS_ETIMEDOUT;
    race->prepare_status = SALTS_EINVAL;
    atomic_store_explicit(&race->ready, 1, memory_order_release);
    while (!atomic_load_explicit(&race->fire, memory_order_acquire) &&
           cmeta_monotonic_ms() < deadline) {
    }
    if (!atomic_load_explicit(&race->fire, memory_order_acquire))
        return;
    race->release_status = scxml_host_router_cancel_source(
        race->router, race->source, &race->cancelled_rows);
    if (race->release_status != SALTS_OK) return;
    race->prepare_status = scxml_host_router_prepare_target(
        race->router, race->source, NULL, 0u,
        "new", 3u, "same", 4u, NULL, 5u, &next);
    if (race->prepare_status == SALTS_OK)
        next.commit(next.user);
}

spec("Bounded CFlow-clock Host delayed send and sendid cancellation") {
    it("publishes only after the virtual-clock boundary and preserves fire-wins") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='waiting'>"
            "<state id='waiting'><onentry>"
            "<send event='elapsed' delay='5ms' id='clock-1'/>"
            "</onentry><transition event='elapsed' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_event_io_binding binding = {0};
        scxml_host_session_ref ref = {0};
        cflow_clock clock = {0};
        cflow_executor executor = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_instance_stats machine = {0};
        cflow_statechart_effect_ticket cancelled_after_fire = {0};
        size_t fired = 0u, delivered = 0u;

        check_true(cflow_clock_virtual_init(&clock, (cflow_instant){100000000u}));
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_compile(&program, source, sizeof(source)-1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u, .event_capacity = 2u,
                .max_text_bytes = 8u, .clock = &clock,
                .timer_capacity = 1u, .cancel_capacity = 2u
            }), SALTS_OK);
        check_true(scxml_host_router_supports_delayed(&router));
        check_equal(scxml_host_router_reserve(&router, &ref), SALTS_OK);
        check_equal(scxml_host_event_io_binding_init_delayed(
            &binding, &router, ref), SALTS_OK);
        {
            scxml_session_config config =
                timed_session_config(&program, &executor, &binding);
            check_equal(scxml_session_init(&session, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_activate(&router, ref, &session), SALTS_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.pending, (size_t)1u);
        check_equal(stats.delayed_pending, (size_t)1u);
        check_equal(stats.committed, UINT64_C(1));
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_EMPTY);
        check_equal(delivered, (size_t)0u);
        check_equal(scxml_host_router_run_due(&router, 1u, &fired), SALTS_OK);
        check_equal(fired, (size_t)0u);
        check_true(cflow_clock_advance(&clock, cflow_duration_from_ms(4u)));
        check_equal(scxml_host_router_run_due(&router, 1u, &fired), SALTS_OK);
        check_equal(fired, (size_t)0u);
        check_true(cflow_clock_advance(&clock, cflow_duration_from_ms(1u)));
        check_equal(scxml_host_router_run_due(&router, 1u, &fired), SALTS_OK);
        check_equal(fired, (size_t)1u);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.delayed_pending, (size_t)0u);
        check_equal(stats.timer_fired, UINT64_C(1));
        check_equal(stats.timer_done_failed, UINT64_C(0));

        /* Once the clock owner has claimed FIRING, a late cancelling ticket
           cannot revoke the already-fired SCXML Event. */
        check_equal(scxml_host_router_prepare_cancel(
            &router, ref, "clock-1", sizeof("clock-1") - 1u,
            &cancelled_after_fire), SALTS_OK);
        cancelled_after_fire.commit(cancelled_after_fire.user);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.cancel_fire_won, UINT64_C(1));
        check_equal(stats.pending, (size_t)1u);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &machine));
        check_true(machine.done);
        check_false(machine.errored);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_host_event_io_binding_destroy(&binding), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        cflow_executor_destroy(&executor);
        cflow_clock_destroy(&clock);
        scxml_program_destroy(&program);
    }

    it("cancels a delayed onentry SEND by sendid in the same microstep") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='waiting'><state id='waiting'><onentry>"
            "<send event='never' delay='5ms' id='clock-2'/>"
            "<cancel sendid='clock-2'/>"
            "</onentry><transition event='never' target='failed'/>"
            "</state><state id='failed'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_event_io_binding binding = {0};
        scxml_host_session_ref ref = {0};
        cflow_clock clock = {0};
        cflow_executor executor = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_instance_stats machine = {0};
        size_t fired = 0u, delivered = 0u;

        check_true(cflow_clock_virtual_init(&clock, (cflow_instant){100000000u}));
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_compile(&program, source, sizeof(source)-1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u, .event_capacity = 2u,
                .max_text_bytes = 8u, .clock = &clock,
                .timer_capacity = 1u, .cancel_capacity = 2u
            }), SALTS_OK);
        check_equal(scxml_host_router_reserve(&router, &ref), SALTS_OK);
        check_equal(scxml_host_event_io_binding_init_delayed(
            &binding, &router, ref), SALTS_OK);
        {
            scxml_session_config config =
                timed_session_config(&program, &executor, &binding);
            check_equal(scxml_session_init(&session, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_activate(&router, ref, &session), SALTS_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.delayed_pending, (size_t)0u);
        check_equal(stats.cancel_prepared, UINT64_C(1));
        check_equal(stats.cancel_committed, UINT64_C(1));
        check_equal(stats.timer_cancelled, UINT64_C(1));
        check_equal(stats.timer_fired, UINT64_C(0));
        check_true(cflow_clock_advance(&clock, cflow_duration_from_ms(10u)));
        check_equal(scxml_host_router_run_due(&router, 1u, &fired), SALTS_OK);
        check_equal(fired, (size_t)0u);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_EMPTY);
        check_true(scxml_session_get_stats(&session, &machine));
        check_false(machine.errored);
        check_false(machine.done);
        scxml_session_cancel(&session);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_host_event_io_binding_destroy(&binding), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        cflow_executor_destroy(&executor);
        cflow_clock_destroy(&clock);
        scxml_program_destroy(&program);
    }

    it("discards cancelled intent transactionally and fails closed on capacity") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='idle'><state id='idle'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_session_ref ref = {0};
        cflow_clock clock = {0};
        cflow_executor executor = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_effect_ticket send_ticket = {0};
        cflow_statechart_effect_ticket cancel_ticket = {0};
        cflow_statechart_effect_ticket rejected = {0};

        check_true(cflow_clock_virtual_init(&clock, (cflow_instant){0u}));
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_compile(&program, source, sizeof(source)-1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_session_init(&session, &(scxml_session_config){
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u
        }), CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u, .event_capacity = 2u,
                .max_text_bytes = 8u, .clock = &clock,
                .timer_capacity = 1u, .cancel_capacity = 1u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &session, &ref), SALTS_OK);
        check_equal(scxml_host_router_prepare_target(
            &router, ref, NULL, 0u, "later", 5u,
            "timer-x", 7u, NULL, 5u, &send_ticket), SALTS_OK);
        check_equal(scxml_host_router_prepare_target(
            &router, ref, NULL, 0u, "other", 5u,
            "timer-y", 7u, NULL, 5u, &rejected), SALTS_ENOBUFS);
        check_equal(scxml_host_router_prepare_cancel(
            &router, ref, "timer-x", 7u, &cancel_ticket), SALTS_OK);
        check_equal(scxml_host_router_prepare_cancel(
            &router, ref, "timer-x", 7u, &rejected), SALTS_ENOBUFS);
        cancel_ticket.discard(cancel_ticket.user);
        send_ticket.commit(send_ticket.user);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.pending, (size_t)1u);
        check_equal(stats.delayed_pending, (size_t)1u);
        check_equal(stats.cancel_discarded, UINT64_C(1));
        check_equal(scxml_host_router_prepare_cancel(
            &router, ref, "timer-x", 7u, &cancel_ticket), SALTS_OK);
        cancel_ticket.commit(cancel_ticket.user);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.delayed_pending, (size_t)0u);
        check_equal(stats.timer_cancelled, UINT64_C(1));
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        cflow_clock_destroy(&clock);
        scxml_program_destroy(&program);
    }
    it("recognizes a fire-won cancel reservation but rejects a truly missing sendid") {
        /* Deterministic model of the brief window between CFlow's registry
           cancel commit and its Host cancel-ticket callback. A Host due claim
           won first, but report_send_done finds the registry already empty.
           Its still-live cancel intent proves the race instead of treating
           that result as an unrelated/invalid delayed send. */
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='waiting'>"
            "<state id='waiting'><transition event='later' target='done'/>"
            "</state><final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_session_ref ref = {0};
        cflow_executor executor = {0};
        cflow_clock clock = {0};
        cflow_statechart_effect_ticket send_ticket = {0};
        cflow_statechart_effect_ticket cancel_ticket = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_instance_stats machine = {0};
        size_t fired = 0u, delivered = 0u;

        check_true(cflow_clock_virtual_init(&clock, (cflow_instant){0u}));
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_compile(&program, source, sizeof(source)-1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_session_init(&session, &(scxml_session_config){
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u, .internal_event_capacity = 2u,
            .completion_capacity = 2u, .microstep_limit = 16u
        }), CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u, .event_capacity = 2u,
                .max_text_bytes = 8u, .clock = &clock,
                .timer_capacity = 1u, .cancel_capacity = 1u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &session, &ref), SALTS_OK);
        check_equal(scxml_host_router_prepare_target(
            &router, ref, NULL, 0u, "later", 5u,
            "cancel-race", 11u, NULL, 5u, &send_ticket), SALTS_OK);
        send_ticket.commit(send_ticket.user);
        check_equal(scxml_host_router_prepare_cancel(
            &router, ref, "cancel-race", 11u, &cancel_ticket), SALTS_OK);
        check_true(cflow_clock_advance(&clock, cflow_duration_from_ms(5u)));
        check_equal(scxml_host_router_run_due(&router, 1u, &fired), SALTS_OK);
        check_equal(fired, (size_t)1u);
        /* Cancel ticket completes after Host already owns the event. */
        cancel_ticket.commit(cancel_ticket.user);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &machine));
        check_true(machine.done);
        check_false(machine.errored);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.timer_fired, UINT64_C(1));
        check_equal(stats.cancel_fire_won, UINT64_C(1));
        check_equal(stats.timer_done_failed, UINT64_C(0));
        check_equal(stats.pending, (size_t)0u);

        /* Without a CFlow sendid registry AND without a real cancel intent,
           an invalid deadline must fail closed rather than invent success. */
        check_equal(scxml_host_router_prepare_target(
            &router, ref, NULL, 0u, "orphan", 6u,
            "orphan-id", 9u, NULL, 5u, &send_ticket), SALTS_OK);
        send_ticket.commit(send_ticket.user);
        check_true(cflow_clock_advance(&clock, cflow_duration_from_ms(5u)));
        check_equal(scxml_host_router_run_due(&router, 1u, &fired),
                    SALTS_EPROTO);
        check_equal(fired, (size_t)0u);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.timer_done_failed, UINT64_C(1));
        check_equal(stats.pending, (size_t)0u);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        cflow_clock_destroy(&clock);
        scxml_program_destroy(&program);
    }

()=>test+insertBefore
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='waiting'>"
            "<state id='waiting'><onentry>"
            "<send event='never' delay='5ms' id='close-1'/>"
            "</onentry></state></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_session_ref ref = {0};
        scxml_host_event_io_binding binding = {0};
        cflow_executor executor = {0};
        cflow_clock clock = {0};
        scxml_host_router_stats stats = {0};
        size_t fired = 0u, delivered = 0u;

        check_true(cflow_clock_virtual_init(&clock, (cflow_instant){0u}));
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_compile(&program, source, sizeof(source)-1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u, .event_capacity = 1u,
                .max_text_bytes = 8u, .clock = &clock,
                .timer_capacity = 1u, .cancel_capacity = 1u
            }), SALTS_OK);
        check_equal(scxml_host_router_reserve(&router, &ref), SALTS_OK);
        check_equal(scxml_host_event_io_binding_init_delayed(
            &binding, &router, ref), SALTS_OK);
        {
            scxml_session_config config =
                timed_session_config(&program, &executor, &binding);
            check_equal(scxml_session_init(&session, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_activate(&router, ref, &session), SALTS_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.delayed_pending, (size_t)1u);
        scxml_session_cancel(&session);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.delayed_pending, (size_t)0u);
        check_equal(stats.timer_cancelled, UINT64_C(1));
        check_true(cflow_clock_advance(&clock, cflow_duration_from_ms(10u)));
        check_equal(scxml_host_router_run_due(&router, 1u, &fired), SALTS_OK);
        check_equal(fired, (size_t)0u);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_EMPTY);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_host_event_io_binding_destroy(&binding), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        cflow_executor_destroy(&executor);
        cflow_clock_destroy(&clock);
        scxml_program_destroy(&program);
    }

}

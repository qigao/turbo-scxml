#include <scxml/host_router.h>

#include <cflow/executor.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <stdatomic.h>
#include <string.h>

typedef struct block_probe {
    atomic_bool entered;
    atomic_bool release;
} block_probe;

static void block_executor(void *user) {
    block_probe *probe = (block_probe *)user;
    atomic_store(&probe->entered, true);
    while (!atomic_load(&probe->release)) cmeta_thread_yield();
}

static scxml_session_config session_config(
    scxml_program *program, cflow_executor *executor) {
    scxml_session_config config = {0};
    config.program = program;
    config.executor = executor;
    config.external_event_capacity = 2u;
    config.internal_event_capacity = 2u;
    config.completion_capacity = 2u;
    config.microstep_limit = 16u;
    return config;
}

spec("Generation-checked bounded SCXML Host routing") {
    it("moves committed Events to two independently owned Statecharts") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' initial='run'>"
            "<state id='run'><transition event='go' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executors[2] = {{0}, {0}};
        scxml_session sessions[2] = {{0}, {0}};
        scxml_host_router router = {0};
        scxml_host_session_ref refs[2] = {{0}, {0}};
        scxml_host_session_ref replacement = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_instance_stats state[2] = {{0}, {0}};
        cflow_statechart_effect_ticket ticket = {0};
        size_t delivered = 0u;
        char text[] = "A";

        check_equal(scxml_compile(&program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);
        for (size_t i = 0u; i < 2u; ++i) {
            scxml_session_config config;
            check_true(cflow_executor_serial_init(&executors[i]));
            config = session_config(&program, &executors[i]);
            check_equal(scxml_session_init(&sessions[i], &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 2u, .event_capacity = 2u,
                .max_text_bytes = 16u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &sessions[0], &refs[0]),
                    SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &sessions[1], &refs[1]),
                    SALTS_OK);
        check_true(refs[0].slot != refs[1].slot);
        check_equal(scxml_host_router_prepare(
            &router, refs[0], "go", 2u, text, 1u, &ticket), SALTS_OK);
        text[0] = 'Z'; /* Prepare already copied the callback-scoped bytes. */
        check_equal(scxml_host_router_drain(&router, 2u, &delivered),
                    CFLOW_MAILBOX_EMPTY);
        check_equal(delivered, (size_t)0u);
        check_equal(scxml_host_router_enqueue(
            &router, refs[1], "go", 2u, "B", 1u), SALTS_OK);
        ticket.commit(ticket.user);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.committed, UINT64_C(2));
        check_equal(scxml_host_router_drain(&router, 2u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)2u);
        for (size_t i = 0u; i < 2u; ++i) {
            check_true(cflow_executor_wait_idle(&executors[i]));
            check_true(scxml_session_get_stats(&sessions[i], &state[i]));
            check_true(state[i].done);
            check_false(state[i].errored);
        }
        check_equal(scxml_host_router_detach(&router, refs[0]), SALTS_OK);
        check_equal(scxml_host_router_attach(
            &router, &sessions[0], &replacement), SALTS_OK);
        check_equal(replacement.slot, refs[0].slot);
        check_true(replacement.generation != refs[0].generation);
        check_equal(scxml_host_router_enqueue(
            &router, refs[0], "go", 2u, "bad", 3u), SALTS_ENOENT);
        check_equal(scxml_host_router_detach(&router, refs[0]), SALTS_ENOENT);
        check_equal(scxml_host_router_detach(&router, replacement), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, refs[1]), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_true(scxml_host_router_is_quiescent(&router));
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(scxml_session_destroy(&sessions[i]),
                        CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executors[i]);
        }
        scxml_program_destroy(&program);
    }

    it("retains the head on Session FIFO FULL until explicit CFlow progress") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' initial='run'>"
            "<state id='run'><transition event='tick'/></state></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_session_ref ref = {0};
        scxml_host_router_stats stats = {0};
        block_probe probe;
        size_t delivered = 0u;
        uint64_t deadline;

        atomic_init(&probe.entered, false);
        atomic_init(&probe.release, false);
        check_equal(scxml_compile(&program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        {
            scxml_session_config config = session_config(&program, &executor);
            check_equal(scxml_session_init(&session, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u, .event_capacity = 3u,
                .max_text_bytes = 16u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &session, &ref), SALTS_OK);
        check_equal(cflow_executor_try_post(
            &executor, block_executor, &probe), CFLOW_ADMISSION_ACCEPTED);
        deadline = cmeta_monotonic_ms() + 3000u;
        while (!atomic_load(&probe.entered) && cmeta_monotonic_ms() < deadline)
            cmeta_thread_yield();
        check_true(atomic_load(&probe.entered));

        for (size_t i = 0u; i < 3u; ++i)
            check_equal(scxml_host_router_enqueue(
                &router, ref, "tick", 4u, "x", 1u), SALTS_OK);
        check_equal(scxml_host_router_enqueue(
            &router, ref, "tick", 4u, "z", 1u), SALTS_ENOBUFS);
        check_equal(scxml_host_router_drain(&router, 3u, &delivered),
                    CFLOW_MAILBOX_FULL);
        check_equal(delivered, (size_t)2u);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_EBUSY);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.pending, (size_t)1u);
        atomic_store(&probe.release, true);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_host_router_drain(&router, 2u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.delivered, UINT64_C(3));
        check_equal(stats.pending, (size_t)0u);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("requires outstanding effect tickets to settle before detaching") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' initial='run'>"
            "<state id='run'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_session_ref ref = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_effect_ticket ticket = {0};

        check_equal(scxml_compile(&program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        {
            scxml_session_config config = session_config(&program, &executor);
            check_equal(scxml_session_init(&session, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u, .event_capacity = 1u,
                .max_text_bytes = 16u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &session, &ref), SALTS_OK);
        check_equal(scxml_host_router_prepare(
            &router, ref, "go", 2u, "hello", 5u, &ticket), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_EBUSY);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_EBUSY);
        ticket.commit(ticket.user);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.cancelled, UINT64_C(1));
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_true(scxml_host_router_is_quiescent(&router));
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("resolves pinned #_parent, #_<invokeid> and public SCXML locations") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' initial='run'>"
            "<state id='run'><transition event='go' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executors[2] = {{0}, {0}};
        scxml_session sessions[2] = {{0}, {0}};
        scxml_host_router router = {0};
        scxml_host_session_ref parent = {0}, child = {0}, resolved = {0};
        scxml_host_session_ref reused = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_instance_stats machine = {0};
        cflow_statechart_effect_ticket ticket = {0};
        char public_location[SCXML_EVENT_METADATA_CAPACITY + 1u];
        size_t location_required = 0u, delivered = 0u;

        check_equal(scxml_compile(&program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);
        for (size_t i = 0u; i < 2u; ++i) {
            scxml_session_config conf;
            check_true(cflow_executor_serial_init(&executors[i]));
            conf = session_config(&program, &executors[i]);
            check_equal(scxml_session_init(&sessions[i], &conf),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 2u, .event_capacity = 2u,
                .max_text_bytes = 16u, .invoke_capacity = 2u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &sessions[0], &parent),
                    SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &sessions[1], &child),
                    SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &sessions[1], &reused),
                    SALTS_EALREADY);
        check_equal(reused.generation, UINT32_C(0));

        check_equal(scxml_host_router_resolve(
            &router, child, "", 0u, &resolved), SALTS_OK);
        check_equal(resolved.slot, child.slot);
        check_equal(resolved.generation, child.generation);
        check_equal(scxml_host_router_resolve(
            &router, child, "#_parent", sizeof("#_parent") - 1u,
            &resolved), SALTS_ENOENT);
        check_equal(scxml_host_router_set_parent(&router, child, parent),
                    SALTS_OK);
        check_equal(scxml_host_router_set_parent(&router, parent, child),
                    SALTS_EINVAL);
        check_equal(scxml_host_router_resolve(
            &router, child, "#_parent", sizeof("#_parent") - 1u,
            &resolved), SALTS_OK);
        check_equal(resolved.slot, parent.slot);
        check_equal(resolved.generation, parent.generation);

        check_equal(scxml_host_router_bind_invoke(
            &router, parent, "child", sizeof("child") - 1u, child), SALTS_OK);
        check_equal(scxml_host_router_bind_invoke(
            &router, parent, "child", sizeof("child") - 1u, child),
            SALTS_EALREADY);
        check_equal(scxml_host_router_bind_invoke(
            &router, parent, "friend", sizeof("friend") - 1u, child), SALTS_OK);
        check_equal(scxml_host_router_bind_invoke(
            &router, parent, "extra", sizeof("extra") - 1u, child), SALTS_ENOBUFS);
        check_equal(scxml_host_router_resolve(
            &router, parent, "#_friend", sizeof("#_friend") - 1u,
            &resolved), SALTS_OK);
        check_equal(resolved.slot, child.slot);
        check_equal(scxml_host_router_resolve(
            &router, child, "#_friend", sizeof("#_friend") - 1u,
            &resolved), SALTS_ENOENT);

        check_equal(scxml_session_copy_location(
            &sessions[0], public_location, sizeof(public_location),
            &location_required), SCXML_LOCATION_OK);
        check_true(location_required > 1u);
        check_equal(scxml_host_router_resolve(
            &router, child, public_location, location_required - 1u,
            &resolved), SALTS_ENOENT);
        check_equal(scxml_host_router_set_external_access(
            &router, parent, true), SALTS_OK);
        check_equal(scxml_host_router_resolve(
            &router, child, public_location, location_required - 1u,
            &resolved), SALTS_OK);
        check_equal(resolved.generation, parent.generation);
        check_equal(scxml_host_router_resolve(
            &router, child, public_location, location_required - 2u,
            &resolved), SALTS_ENOENT);

        check_equal(scxml_host_router_prepare_target(
            &router, child, "#_parent", sizeof("#_parent") - 1u,
            "go", 2u, "send-42", 7u,
            &(scxml_content_view){.kind = SCXML_CONTENT_TEXT_UTF8,
                .bytes = "hi", .byte_count = 2u}, 0u, &ticket), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, child), SALTS_EBUSY);
        check_equal(scxml_host_router_detach(&router, parent), SALTS_EBUSY);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_EMPTY);
        ticket.commit(ticket.user);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_true(cflow_executor_wait_idle(&executors[0]));
        check_true(scxml_session_get_stats(&sessions[0], &machine));
        check_true(machine.done);
        check_false(machine.errored);
        check_equal(scxml_host_router_prepare_target(
            &router, parent, "#_child", sizeof("#_child") - 1u,
            "go", 2u, NULL, 0u, NULL, 0u, &ticket), SALTS_OK);
        ticket.discard(ticket.user);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.discarded, UINT64_C(1));
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.invoke_bindings, (size_t)2u);

        check_equal(scxml_host_router_unbind_invoke(
            &router, parent, "child", sizeof("child") - 1u), SALTS_OK);
        check_equal(scxml_host_router_unbind_invoke(
            &router, parent, "friend", sizeof("friend") - 1u), SALTS_OK);
        check_equal(scxml_host_router_set_parent(
            &router, child, (scxml_host_session_ref){0}), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, parent), SALTS_OK);
        check_equal(scxml_host_router_attach(
            &router, &sessions[0], &reused), SALTS_OK);
        check_equal(reused.slot, parent.slot);
        check_true(reused.generation != parent.generation);
        check_equal(scxml_host_router_resolve(
            &router, child, public_location, location_required - 1u,
            &resolved), SALTS_ENOENT);
        check_equal(scxml_host_router_set_external_access(
            &router, parent, true), SALTS_ENOENT);
        check_equal(scxml_host_router_detach(&router, reused), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, child), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_true(scxml_host_router_is_quiescent(&router));
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(scxml_session_destroy(&sessions[i]),
                        CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executors[i]);
        }
        scxml_program_destroy(&program);
    }
}

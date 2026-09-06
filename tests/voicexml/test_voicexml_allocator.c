#include <voicexml/voicexml.h>

#include "tinytest.h"
#include "voicexml_test_allocator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { TRACKED_ALLOCATION_CAPACITY = 32 };

typedef struct allocation_tracker {
    size_t calls;
    size_t fail_on_call;
    size_t live_count;
    size_t invalid_operations;
    void *live[TRACKED_ALLOCATION_CAPACITY];
} allocation_tracker;

static allocation_tracker tracker;

static void tracker_reset(size_t fail_on_call) {
    memset(&tracker, 0, sizeof(tracker));
    tracker.fail_on_call = fail_on_call;
}

static bool tracker_should_fail(void) {
    ++tracker.calls;
    return tracker.fail_on_call != 0u &&
           tracker.calls == tracker.fail_on_call;
}

static void tracker_add(void *pointer) {
    if (pointer == NULL) return;
    if (tracker.live_count >= TRACKED_ALLOCATION_CAPACITY) {
        ++tracker.invalid_operations;
        return;
    }
    tracker.live[tracker.live_count++] = pointer;
}

static size_t tracker_find(void *pointer) {
    size_t index;
    for (index = 0u; index < tracker.live_count; ++index) {
        if (tracker.live[index] == pointer) return index;
    }
    return TRACKED_ALLOCATION_CAPACITY;
}

static void *tracked_malloc(size_t size) {
    void *pointer;
    if (tracker_should_fail()) return NULL;
    pointer = malloc(size);
    tracker_add(pointer);
    return pointer;
}

static void *tracked_calloc(size_t count, size_t size) {
    void *pointer;
    if (tracker_should_fail()) return NULL;
    pointer = calloc(count, size);
    tracker_add(pointer);
    return pointer;
}

static void *tracked_realloc(void *pointer, size_t size) {
    const size_t old_index = pointer != NULL
        ? tracker_find(pointer) : TRACKED_ALLOCATION_CAPACITY;
    void *replacement;
    if (tracker_should_fail()) return NULL;
    replacement = realloc(pointer, size);
    if (replacement == NULL) return NULL;
    if (pointer == NULL) {
        tracker_add(replacement);
    } else if (old_index < TRACKED_ALLOCATION_CAPACITY) {
        tracker.live[old_index] = replacement;
    } else {
        ++tracker.invalid_operations;
        tracker_add(replacement);
    }
    return replacement;
}

static void tracked_free(void *pointer) {
    const size_t index = tracker_find(pointer);
    if (pointer == NULL) return;
    if (index >= TRACKED_ALLOCATION_CAPACITY) {
        ++tracker.invalid_operations;
        free(pointer);
        return;
    }
    tracker.live[index] = tracker.live[tracker.live_count - 1u];
    --tracker.live_count;
    free(pointer);
}

static const vxml_test_allocator test_allocator = {
    tracked_malloc,
    tracked_calloc,
    tracked_realloc,
    tracked_free};

static vxml_status compile_allocation_fixture(
    vxml_program *program, vxml_diagnostic *diagnostic) {
    static const char source[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<form id='a'><block/></form>"
        "<form id='b'><block/></form>"
        "<form id='c'><block/></form>"
        "<form id='d'><block/></form>"
        "<form id='e'><block/></form>"
        "</vxml>";
    return vxml_compile(
        source, sizeof(source) - 1u, NULL, program, diagnostic);
}

spec("VoiceXML allocator cleanup") {
    after_each() {
        check_equal(tracker.live_count, (size_t)0u);
        check_equal(tracker.invalid_operations, (size_t)0u);
        vxml_test_allocator_reset();
    }

    it("releases every compiler allocation on deterministic failure") {
        static const char *allocation_points[] = {
            "first decoded id",
            "initial temporary id table",
            "second decoded id",
            "third decoded id",
            "fourth decoded id",
            "fifth decoded id",
            "grown temporary id table",
            "final immutable program"};
        size_t failure;

        for (failure = 1u;
             failure <= sizeof(allocation_points) / sizeof(allocation_points[0]);
             ++failure) {
            vxml_program program = {(void *)1};
            vxml_diagnostic diagnostic = {0};

            tracker_reset(failure);
            vxml_test_allocator_set(&test_allocator);
            info("allocation point %zu: %s", failure,
                 allocation_points[failure - 1u]);
            check_equal(
                compile_allocation_fixture(&program, &diagnostic),
                VXML_ALLOCATION_FAILED);
            check_null(program.impl);
            check_equal(diagnostic.status, VXML_ALLOCATION_FAILED);
            check_not_null(strstr(diagnostic.message, "allocation"));
            check_equal(tracker.calls, failure);
            check_equal(tracker.live_count, (size_t)0u);
            check_equal(tracker.invalid_operations, (size_t)0u);
            vxml_test_allocator_reset();
        }

        tracker_reset(0u);
        vxml_test_allocator_set(&test_allocator);
        {
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};
            check_equal(
                compile_allocation_fixture(&program, &diagnostic), VXML_OK);
            check_equal(tracker.calls,
                        sizeof(allocation_points) / sizeof(allocation_points[0]));
            check_equal(tracker.live_count, (size_t)1u);
            vxml_program_destroy(&program);
            check_equal(tracker.live_count, (size_t)0u);
        }
    }

    it("leaves session output empty when its allocation fails") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block/></form></vxml>";
        vxml_program program = {0};
        vxml_session session = {(void *)1};

        check_equal(vxml_compile(
                        source, sizeof(source) - 1u, NULL, &program, NULL),
                    VXML_OK);
        tracker_reset(1u);
        vxml_test_allocator_set(&test_allocator);
        check_equal(vxml_session_init(&session, &program),
                    VXML_ALLOCATION_FAILED);
        check_null(session.impl);
        check_equal(tracker.calls, (size_t)1u);
        check_equal(tracker.live_count, (size_t)0u);
        check_equal(tracker.invalid_operations, (size_t)0u);

        tracker_reset(0u);
        check_equal(vxml_session_init(&session, &program), VXML_OK);
        check_equal(tracker.calls, (size_t)1u);
        check_equal(tracker.live_count, (size_t)1u);
        vxml_session_destroy(&session);
        check_equal(tracker.live_count, (size_t)0u);
        vxml_test_allocator_reset();
        vxml_program_destroy(&program);
    }
}

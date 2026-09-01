#include "scxml_program.h"
#include "scxml_analyze.h"
#include "scxml_emit.h"
#include "scxml_runtime.h"

scxml_limits scxml_default_limits(void) {
    const scxml_limits limits = {
        {16u * 1024u * 1024u, 1048576u, 1048576u, 256u,
         32u * 1024u * 1024u},
        SCXML_DEFAULT_MAX_STATES,
        SCXML_DEFAULT_MAX_EVENTS,
        SCXML_DEFAULT_MAX_TRANSITIONS,
        SCXML_DEFAULT_MAX_NAME_BYTES};
    return limits;
}

scxml_cmeta_compile_options_v1
scxml_cmeta_default_compile_options(const cmeta_data_desc *root) {
    const scxml_expr_limits limits =
        scxml_expr_default_limits();
    const scxml_cmeta_compile_options_v1 options = {
        .abi_version = SCXML_CMETA_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(scxml_cmeta_compile_options_v1),
        .root = root,
        .max_source_bytes = limits.max_source_bytes,
        .max_instructions = limits.max_instructions,
        .max_operands = limits.max_operands,
        .max_expression_depth = limits.max_expression_depth,
        .max_path_depth = limits.max_path_depth,
        .max_literal_bytes = limits.max_literal_bytes,
        .max_string_bytes = limits.max_string_bytes,
        .max_iterations = SCXML_CMETA_DEFAULT_MAX_ITERATIONS
    };
    return options;
}

static scxml_status compile_scxml_model(
    scxml_program *out, const char *input, size_t input_size,
    const scxml_limits *limits_or_null,
    scxml_data_model data_model, const cmeta_data_desc *cmeta_root,
    const scxml_expr_limits *expression_limits,
    size_t cmeta_max_iterations,
    scxml_diagnostic *diagnostic) {
    scxml_limits limits = limits_or_null != NULL
                                    ? *limits_or_null
                                    : scxml_default_limits();
    turbo_xml_document document = {0};
    turbo_xml_diagnostic xml_diagnostic = {0};
    turbo_xml_node xml_root;
    scxml_ast ast = {0};
    scxml_ast_limits ast_limits = {0};
    scxml_syntax_node root = {0};
    scxml_build build;
    scxml_counts counts = {0};
    scxml_program_impl *impl = NULL;
    cflow_statechart_definition definition;
    cflow_statechart_definition_v2 definition_v2;
    cflow_statechart_status native_status;
    scxml_status status;
    scxml_syntax_attribute version;
    scxml_syntax_attribute datamodel;
    scxml_syntax_attribute binding;
    scxml_syntax_attribute document_name_attribute;
    turbo_xml_string_view document_name = {NULL, 0u};
    size_t index;
    size_t name_bytes = 0u;
    size_t retained_string_bytes = 0u;
    size_t action_ref_count = 0u;
    size_t transition_capacity = 0u;
    size_t transition_target_capacity = 0u;
    size_t guard_capacity = 0u;
    size_t transition_action_capacity = 0u;
    size_t descriptor_extra_multiplier = 0u;
    char *name_cursor;
    bool needs_execution_error = false;

    memset(&build, 0, sizeof(build));
    build.limits = limits;
    build.diagnostic = diagnostic;
    build.data_model = data_model;
    build.cmeta_root = cmeta_root;
    build.max_iterations = cmeta_max_iterations;
    if (expression_limits != NULL)
        build.expression_limits = *expression_limits;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out == NULL || out->impl != NULL || input == NULL || input_size == 0u ||
        limits.max_states == 0u || limits.max_events == 0u ||
        limits.max_transitions == 0u || limits.max_name_bytes == 0u) {
        return scxml_analyze_fail(&build, SCXML_INVALID_ARGUMENT,
                          (turbo_xml_location){0u, 0u, 0u},
                          "output/input and all SCXML limits must be valid");
    }
    switch (turbo_xml_parse(&document, input, input_size, &limits.xml,
                            &xml_diagnostic)) {
        case TURBO_XML_OK: break;
        case TURBO_XML_LIMIT_EXCEEDED:
            return scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
        case TURBO_XML_ALLOCATION_FAILED:
            return scxml_analyze_fail(&build, SCXML_ALLOCATION_FAILED,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
        default:
            return scxml_analyze_fail(&build, SCXML_XML_ERROR,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
    }
    xml_root = turbo_xml_document_root(&document);
    ast_limits.max_nodes = limits.xml.max_nodes;
    ast_limits.max_attributes = limits.xml.max_attributes;
    ast_limits.max_depth = limits.xml.max_depth;
    if (!scxml_analyze_checked_add(
            limits.xml.max_input_bytes,
            limits.xml.max_retained_string_bytes,
            &ast_limits.max_storage_bytes)) {
        status = scxml_analyze_fail(
            &build, SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(xml_root),
            "SCXML AST storage limit overflow");
        goto cleanup;
    }
    status = scxml_ast_build(
        &ast, xml_root, &ast_limits, diagnostic);
    if (status != SCXML_OK) goto cleanup;
    turbo_xml_document_destroy(&document);
    root = scxml_syntax_root(&ast);
    if (scxml_analyze_element_kind(root) != SCXML_ELEMENT_SCXML ||
        !scxml_analyze_view_equal_raw(scxml_syntax_node_namespace_uri(root),
                        SCXML_NAMESPACE)) {
        status = scxml_analyze_fail(&build, SCXML_INVALID_NAMESPACE,
                            scxml_syntax_node_location(root),
                            "root must be W3C SCXML scxml element");
        goto cleanup;
    }
    status = scxml_analyze_validate_element_attributes(&build, root, SCXML_ELEMENT_SCXML);
    if (status != SCXML_OK) goto cleanup;
    version = scxml_analyze_find_attribute(root, "version");
    if (version.impl == NULL ||
        !scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(version), "1.0")) {
        status = scxml_analyze_fail(
            &build, SCXML_INVALID_VERSION,
            version.impl != NULL ? scxml_syntax_attribute_location(version)
                                 : scxml_syntax_node_location(root),
            "SCXML version must be 1.0");
        goto cleanup;
    }
    datamodel = scxml_analyze_find_attribute(root, "datamodel");
    binding = scxml_analyze_find_attribute(root, "binding");
    if (binding.impl != NULL &&
        !scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(binding), "early")) {
        if (!scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(binding), "late")) {
            status = scxml_analyze_fail(&build, SCXML_INVALID_STRUCTURE,
                                scxml_syntax_attribute_location(binding),
                                "binding must be 'early' or 'late'");
            goto cleanup;
        }
        if (data_model != SCXML_DATA_MODEL_CMETA) {
            status = scxml_analyze_fail(
                &build, SCXML_UNSUPPORTED_FEATURE,
                scxml_syntax_attribute_location(binding),
                "binding='late' requires the CMeta data model");
            goto cleanup;
        }
        build.late_binding = true;
    }
    if (data_model == SCXML_DATA_MODEL_NULL && datamodel.impl != NULL &&
        !scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(datamodel), "null")) {
        status = scxml_analyze_fail(&build, SCXML_UNSUPPORTED_DATAMODEL,
                            scxml_syntax_attribute_location(datamodel),
                            "only the SCXML null data model is supported");
        goto cleanup;
    }
    if (data_model == SCXML_DATA_MODEL_CMETA &&
        (datamodel.impl == NULL ||
         !scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(datamodel), "cmeta"))) {
        status = scxml_analyze_fail(
            &build, SCXML_UNSUPPORTED_DATAMODEL,
            datamodel.impl != NULL ? scxml_syntax_attribute_location(datamodel)
                                   : scxml_syntax_node_location(root),
            "CMeta compilation requires datamodel='cmeta'");
        goto cleanup;
    }
    document_name_attribute = scxml_analyze_find_attribute(root, "name");
    if (document_name_attribute.impl != NULL) {
        document_name = scxml_syntax_attribute_value(document_name_attribute);
        if (!scxml_analyze_is_xml_nmtoken(document_name)) {
            status = scxml_analyze_fail(
                &build, SCXML_INVALID_STRUCTURE,
                scxml_syntax_attribute_location(document_name_attribute),
                "SCXML name must be one XML NMTOKEN");
            goto cleanup;
        }
    }
    status = scxml_analyze_state(&build, root, SCXML_ELEMENT_SCXML, true, &counts);
    if (status != SCXML_OK) goto cleanup;
    needs_execution_error =
        counts.assignment_rows != 0u || counts.foreach_rows != 0u ||
        counts.dynamic_expression_rows != 0u ||
        (data_model == SCXML_DATA_MODEL_CMETA &&
         counts.conditional_branches != 0u);
    if (((counts.requirements &
          (SCXML_REQUIREMENT_EVENT_IO |
           SCXML_REQUIREMENT_INVOKE)) != 0u ||
         needs_execution_error) &&
        !scxml_analyze_checked_add(counts.event_occurrences, 2u,
                     &counts.event_occurrences)) {
        status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                            scxml_syntax_node_location(root),
                            "reserved SCXML error event count overflow");
        goto cleanup;
    }
    transition_capacity = counts.transition_rows;
    transition_target_capacity = counts.transition_target_rows;
    guard_capacity = counts.guard_rows;
    transition_action_capacity = counts.transition_action_rows;
    if (!scxml_analyze_checked_add(counts.event_occurrences, counts.state_names,
                     &descriptor_extra_multiplier)) {
        status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                            scxml_syntax_node_location(root),
                            "event descriptor expansion bound overflow");
        goto cleanup;
    }
    if (descriptor_extra_multiplier != 0u)
        --descriptor_extra_multiplier;
    {
        size_t extra;
        if (!scxml_analyze_checked_multiply(counts.event_descriptor_rows,
                              descriptor_extra_multiplier, &extra) ||
            !scxml_analyze_checked_add(transition_capacity, extra,
                         &transition_capacity) ||
            !scxml_analyze_checked_multiply(counts.event_descriptor_target_rows,
                              descriptor_extra_multiplier, &extra) ||
            !scxml_analyze_checked_add(transition_target_capacity, extra,
                         &transition_target_capacity) ||
            !scxml_analyze_checked_multiply(counts.event_descriptor_guard_rows,
                              descriptor_extra_multiplier, &extra) ||
            !scxml_analyze_checked_add(guard_capacity, extra, &guard_capacity) ||
            !scxml_analyze_checked_multiply(counts.event_descriptor_action_rows,
                              descriptor_extra_multiplier, &extra) ||
            !scxml_analyze_checked_add(transition_action_capacity, extra,
                         &transition_action_capacity)) {
            status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                                scxml_syntax_node_location(root),
                                "event descriptor expansion overflow");
            goto cleanup;
        }
    }
    if (!scxml_analyze_checked_add(counts.state_action_rows,
                     transition_action_capacity,
                     &action_ref_count) ||
        counts.state_rows > limits.max_states ||
        transition_capacity > limits.max_transitions ||
        counts.state_rows > UINT32_MAX ||
        transition_capacity > UINT32_MAX ||
        transition_target_capacity > CFLOW_STATECHART_MAX_TARGET_REFS ||
        guard_capacity > CFLOW_MACHINE_MAX_GUARDS ||
        counts.executable_blocks > CFLOW_MACHINE_MAX_ACTIONS ||
        counts.state_action_rows > CFLOW_STATECHART_MAX_ACTION_REFS ||
        transition_action_capacity > CFLOW_STATECHART_MAX_ACTION_REFS ||
        action_ref_count > CFLOW_STATECHART_MAX_ACTION_REFS) {
        status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                            scxml_syntax_node_location(root),
                            "SCXML state or transition count exceeds limits");
        goto cleanup;
    }

    build.states = scxml_emit_allocate_rows(counts.state_rows, sizeof(*build.states));
    build.transitions =
        scxml_emit_allocate_rows(transition_capacity, sizeof(*build.transitions));
    build.transition_targets = scxml_emit_allocate_rows(
        transition_target_capacity, sizeof(*build.transition_targets));
    build.guards = scxml_emit_allocate_rows(guard_capacity, sizeof(*build.guards));
    build.events =
        scxml_emit_allocate_rows(counts.event_occurrences, sizeof(*build.events));
    build.executables = scxml_emit_allocate_rows(counts.executable_blocks,
                                      sizeof(*build.executables));
    build.state_actions = scxml_emit_allocate_rows(counts.state_action_rows,
                                        sizeof(*build.state_actions));
    build.transition_actions = scxml_emit_allocate_rows(
        transition_action_capacity, sizeof(*build.transition_actions));
    build.bindings = scxml_emit_allocate_rows(counts.executable_blocks,
                                   sizeof(*build.bindings));
    build.guard_bindings = scxml_emit_allocate_rows(
        guard_capacity, sizeof(*build.guard_bindings));
    build.guard_users = scxml_emit_allocate_rows(
        guard_capacity, sizeof(*build.guard_users));
    build.blocks = scxml_emit_allocate_rows(counts.block_rows,
                                 sizeof(*build.blocks));
    build.steps = scxml_emit_allocate_rows(counts.executable_steps,
                                sizeof(*build.steps));
    build.branches = scxml_emit_allocate_rows(
        counts.conditional_branches, sizeof(*build.branches));
    build.effects = scxml_emit_allocate_rows(counts.effect_rows, sizeof(*build.effects));
    build.payloads = scxml_emit_allocate_rows(
        counts.payload_rows, sizeof(*build.payloads));
    build.assignments = scxml_emit_allocate_rows(
        counts.assignment_rows, sizeof(*build.assignments));
    build.foreach_descriptors = scxml_emit_allocate_rows(
        counts.foreach_rows, sizeof(*build.foreach_descriptors));
    build.invocations = scxml_emit_allocate_rows(
        counts.invocation_rows, sizeof(*build.invocations));
    build.invocation_names = scxml_emit_allocate_rows(
        counts.invocation_rows, sizeof(*build.invocation_names));
    build.done_data = scxml_emit_allocate_rows(
        counts.done_data_rows, sizeof(*build.done_data));
    build.log_storage = counts.log_label_bytes != 0u
                            ? (char *)calloc(counts.log_label_bytes, 1u)
                            : NULL;
    build.effect_storage = counts.effect_string_bytes != 0u
                               ? (char *)calloc(counts.effect_string_bytes, 1u)
                               : NULL;
    build.invocation_storage = counts.invocation_string_bytes != 0u
                                   ? (char *)calloc(
                                         counts.invocation_string_bytes, 1u)
                                   : NULL;
    build.step_capacity = counts.executable_steps;
    build.branch_capacity = counts.conditional_branches;
    build.effect_capacity = counts.effect_rows;
    build.payload_capacity = counts.payload_rows;
    build.assignment_capacity = counts.assignment_rows;
    build.foreach_capacity = counts.foreach_rows;
    build.log_storage_capacity = counts.log_label_bytes;
    build.effect_storage_capacity = counts.effect_string_bytes;
    build.invocation_storage_capacity = counts.invocation_string_bytes;
    build.invocation_capacity = counts.invocation_rows;
    build.done_data_capacity = counts.done_data_rows;
    build.guard_capacity = guard_capacity;
    build.transition_target_capacity = transition_target_capacity;
    build.max_conditional_depth = counts.max_conditional_depth;
    build.requirements = counts.requirements;
    build.state_names =
        scxml_emit_allocate_rows(counts.state_names, sizeof(*build.state_names));
    build.event_names =
        scxml_emit_allocate_rows(counts.event_occurrences, sizeof(*build.event_names));
    build.event_occurrences = scxml_emit_allocate_rows(
        counts.event_occurrences, sizeof(*build.event_occurrences));
    build.node_refs =
        scxml_emit_allocate_rows(counts.node_refs, sizeof(*build.node_refs));
    build.synthetic_initials = scxml_emit_allocate_rows(
        counts.synthetic_initials, sizeof(*build.synthetic_initials));
    if ((counts.state_rows != 0u && build.states == NULL) ||
        (transition_capacity != 0u && build.transitions == NULL) ||
        (transition_target_capacity != 0u &&
         build.transition_targets == NULL) ||
        (guard_capacity != 0u &&
         (build.guards == NULL || build.guard_bindings == NULL ||
          build.guard_users == NULL)) ||
        (counts.event_occurrences != 0u &&
          (build.events == NULL || build.event_names == NULL ||
           build.event_occurrences == NULL)) ||
        (counts.executable_blocks != 0u &&
         (build.executables == NULL || build.bindings == NULL)) ||
        (counts.block_rows != 0u && build.blocks == NULL) ||
        (counts.executable_steps != 0u && build.steps == NULL) ||
        (counts.conditional_branches != 0u && build.branches == NULL) ||
        (counts.effect_rows != 0u && build.effects == NULL) ||
        (counts.payload_rows != 0u && build.payloads == NULL) ||
        (counts.assignment_rows != 0u && build.assignments == NULL) ||
        (counts.foreach_rows != 0u &&
         build.foreach_descriptors == NULL) ||
        (counts.invocation_rows != 0u &&
         (build.invocations == NULL || build.invocation_names == NULL)) ||
        (counts.done_data_rows != 0u && build.done_data == NULL) ||
        (counts.log_label_bytes != 0u && build.log_storage == NULL) ||
        (counts.effect_string_bytes != 0u &&
         build.effect_storage == NULL) ||
        (counts.invocation_string_bytes != 0u &&
         build.invocation_storage == NULL) ||
        (counts.state_action_rows != 0u && build.state_actions == NULL) ||
        (transition_action_capacity != 0u &&
         build.transition_actions == NULL) ||
        (counts.state_names != 0u && build.state_names == NULL) ||
        (counts.node_refs != 0u && build.node_refs == NULL) ||
        (counts.synthetic_initials != 0u &&
         build.synthetic_initials == NULL)) {
        status = scxml_analyze_fail(&build, SCXML_ALLOCATION_FAILED,
                            scxml_syntax_node_location(root),
                            "unable to allocate bounded SCXML declarations");
        goto cleanup;
    }
    status = scxml_analyze_emit_state(&build, root, 0u, true);
    if (status != SCXML_OK) goto cleanup;
    qsort(build.state_names, build.state_name_index,
          sizeof(*build.state_names), scxml_analyze_compare_name_ref);
    {
        const scxml_name_ref *duplicate = scxml_analyze_find_earliest_duplicate(
            build.state_names, build.state_name_index);
        if (duplicate != NULL) {
            status = scxml_analyze_fail(&build, SCXML_DUPLICATE_ID,
                                duplicate->location,
                                "duplicate SCXML state id");
            goto cleanup;
        }
    }
    qsort(build.node_refs, build.node_ref_index, sizeof(*build.node_refs),
          scxml_analyze_compare_node_ref);
    status = scxml_analyze_emit_invocation_declarations(
        &build, root, build.node_ref_index);
    if (status != SCXML_OK) goto cleanup;
    qsort(build.invocation_names, build.invocation_index,
          sizeof(*build.invocation_names), scxml_analyze_compare_name_ref);
    {
        const scxml_name_ref *duplicate = scxml_analyze_find_earliest_duplicate(
            build.invocation_names, build.invocation_index);
        if (duplicate != NULL) {
            status = scxml_analyze_fail(&build, SCXML_DUPLICATE_ID,
                                duplicate->location,
                                "duplicate SCXML invoke id");
            goto cleanup;
        }
    }
    status = scxml_analyze_collect_transition_events(&build, root);
    if (status != SCXML_OK) goto cleanup;
    if ((build.requirements &
         (SCXML_REQUIREMENT_EVENT_IO |
          SCXML_REQUIREMENT_INVOKE)) != 0u ||
        needs_execution_error)
        scxml_analyze_collect_reserved_error_events(&build, scxml_syntax_node_location(root));
    status = scxml_analyze_build_event_names(&build, build.event_occurrence_index);
    if (status != SCXML_OK) goto cleanup;
    if (needs_execution_error) {
        const turbo_xml_string_view execution_name = {
            SCXML_ERROR_EXECUTION_EVENT,
            sizeof(SCXML_ERROR_EXECUTION_EVENT) - 1u};
        const scxml_name_ref *execution = scxml_analyze_find_name_ref(
            build.event_names, build.event_name_count, execution_name);
        if (execution == NULL) {
            status = scxml_analyze_fail(&build, SCXML_NATIVE_IR_REJECTED,
                                scxml_syntax_node_location(root),
                                "reserved execution error event was not retained");
            goto cleanup;
        }
        build.execution_error_event = (cflow_event_id)execution->id;
    }
    status = scxml_analyze_resolve_invocation_events(&build);
    if (status != SCXML_OK) goto cleanup;
    status = scxml_emit_data_initializers(&build, root);
    if (status != SCXML_OK) goto cleanup;
    status = scxml_emit_done_data(&build, root, build.node_ref_index, 0u);
    if (status != SCXML_OK) goto cleanup;
    status = scxml_emit_state_executables(&build, root, build.node_ref_index);
    if (status != SCXML_OK) goto cleanup;
    status = scxml_emit_transitions(&build, root, build.node_ref_index,
                              build.synthetic_index);
    if (status != SCXML_OK) goto cleanup;
    if (build.effect_index != counts.effect_rows ||
        build.payload_index != counts.payload_rows ||
        build.assignment_index != counts.assignment_rows ||
        build.foreach_index != counts.foreach_rows ||
        build.effect_storage_index != counts.effect_string_bytes ||
        build.invocation_index != counts.invocation_rows ||
        build.invocation_emit_index != counts.invocation_rows ||
        build.done_data_index != counts.done_data_rows ||
        build.late_initializer_index != counts.late_initializer_rows ||
        build.block_index != counts.block_rows ||
        build.invocation_storage_index !=
            counts.invocation_string_bytes) {
        status = scxml_analyze_fail(&build, SCXML_NATIVE_IR_REJECTED,
                            scxml_syntax_node_location(root),
                            "effect descriptor emission mismatched admission");
        goto cleanup;
    }
    if (!scxml_analyze_checked_add(name_bytes, document_name.size, &name_bytes)) {
        status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                            scxml_syntax_node_location(root),
                            "retained SCXML name size overflow");
        goto cleanup;
    }
    for (index = 0u; index < build.state_name_index; ++index) {
        if (!scxml_analyze_checked_add(name_bytes, build.state_names[index].name.size,
                         &name_bytes)) {
            status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                                scxml_syntax_node_location(root),
                                "retained SCXML name size overflow");
            goto cleanup;
        }
    }
    for (index = 0u; index < build.event_name_count; ++index) {
        if (!scxml_analyze_checked_add(name_bytes, build.event_names[index].name.size,
                         &name_bytes)) {
            status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                                scxml_syntax_node_location(root),
                                "retained SCXML name size overflow");
            goto cleanup;
        }
    }
    if (!scxml_analyze_checked_add(name_bytes, counts.log_label_bytes,
                     &retained_string_bytes) ||
        !scxml_analyze_checked_add(retained_string_bytes, counts.effect_string_bytes,
                     &retained_string_bytes) ||
        !scxml_analyze_checked_add(retained_string_bytes,
                     counts.invocation_string_bytes,
                     &retained_string_bytes)) {
        status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                            scxml_syntax_node_location(root),
                            "retained SCXML string size overflow");
        goto cleanup;
    }
    if (retained_string_bytes > limits.max_name_bytes) {
        status = scxml_analyze_fail(&build, SCXML_LIMIT_EXCEEDED,
                            scxml_syntax_node_location(root),
                            "retained SCXML strings exceed max_name_bytes");
        goto cleanup;
    }

    memset(&definition, 0, sizeof(definition));
    definition.state_type = data_model == SCXML_DATA_MODEL_CMETA
                                ? cmeta_root->storage_type
                                : &cmeta_type_bool;
    definition.states = build.states;
    definition.state_count = build.state_index;
    definition.events = build.events;
    definition.event_count = build.event_name_count;
    definition.guards = build.guards;
    definition.guard_count = build.guard_index;
    definition.executables = build.executables;
    definition.executable_count = build.executable_index;
    definition.transitions = build.transitions;
    definition.transition_count = build.transition_index;
    definition.state_actions = build.state_actions;
    definition.state_action_count = build.state_action_index;
    definition.transition_actions = build.transition_actions;
    definition.transition_action_count = build.transition_action_index;
    definition_v2 = (cflow_statechart_definition_v2){
        .abi_version = CFLOW_STATECHART_DEFINITION_ABI_V2,
        .struct_size = sizeof(definition_v2),
        .base = definition,
        .transition_targets = build.transition_targets,
        .transition_target_count = build.transition_target_index};
    impl = (scxml_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        status = scxml_analyze_fail(&build, SCXML_ALLOCATION_FAILED,
                            scxml_syntax_node_location(root),
                            "unable to allocate SCXML program");
        goto cleanup;
    }
    native_status = cflow_statechart_build_v2(
        &impl->statechart, &definition_v2);
    if (native_status != CFLOW_STATECHART_OK) {
        char message[SCXML_DIAGNOSTIC_CAPACITY];
        (void)snprintf(message, sizeof(message),
                       "native Statechart rejected SCXML lowering (status=%d)",
                       (int)native_status);
        status = scxml_analyze_fail(&build, SCXML_NATIVE_IR_REJECTED,
                            scxml_syntax_node_location(root), message);
        goto cleanup;
    }
    impl->state_names = scxml_emit_allocate_rows(build.state_name_index,
                                      sizeof(*impl->state_names));
    impl->event_names = scxml_emit_allocate_rows(build.event_name_count,
                                      sizeof(*impl->event_names));
    impl->event_names_by_id = scxml_emit_allocate_rows(
        build.event_name_count, sizeof(*impl->event_names_by_id));
    impl->name_storage = name_bytes != 0u ? (char *)malloc(name_bytes) : NULL;
    if ((build.state_name_index != 0u && impl->state_names == NULL) ||
        (build.event_name_count != 0u && impl->event_names == NULL) ||
        (build.event_name_count != 0u &&
         impl->event_names_by_id == NULL) ||
        (name_bytes != 0u && impl->name_storage == NULL)) {
        status = scxml_analyze_fail(&build, SCXML_ALLOCATION_FAILED,
                            scxml_syntax_node_location(root),
                            "unable to retain SCXML name mappings");
        goto cleanup;
    }
    name_cursor = impl->name_storage;
    scxml_emit_copy_program_names(impl->state_names, build.state_names,
                       build.state_name_index, &name_cursor);
    scxml_emit_copy_program_names(impl->event_names, build.event_names,
                       build.event_name_count, &name_cursor);
    impl->document_name_size = document_name.size;
    if (document_name.size != 0u) {
        impl->document_name = name_cursor;
        memcpy(name_cursor, document_name.data, document_name.size);
        name_cursor += document_name.size;
    } else {
        impl->document_name = "";
    }
    impl->state_name_count = build.state_name_index;
    impl->event_name_count = build.event_name_count;
    impl->data_model = data_model;
    impl->cmeta_root = cmeta_root;
    impl->requirements = build.requirements;
    if ((build.requirements &
         (SCXML_REQUIREMENT_EVENT_IO |
          SCXML_REQUIREMENT_INVOKE)) != 0u ||
        needs_execution_error) {
        const turbo_xml_string_view execution_name = {
            SCXML_ERROR_EXECUTION_EVENT,
            sizeof(SCXML_ERROR_EXECUTION_EVENT) - 1u};
        const turbo_xml_string_view communication_name = {
            SCXML_ERROR_COMMUNICATION_EVENT,
            sizeof(SCXML_ERROR_COMMUNICATION_EVENT) - 1u};
        const scxml_name_ref *execution = scxml_analyze_find_name_ref(
            build.event_names, build.event_name_count, execution_name);
        const scxml_name_ref *communication = scxml_analyze_find_name_ref(
            build.event_names, build.event_name_count, communication_name);
        if (execution == NULL || communication == NULL) {
            status = scxml_analyze_fail(&build, SCXML_NATIVE_IR_REJECTED,
                                scxml_syntax_node_location(root),
                                "reserved SCXML error events were not retained");
            goto cleanup;
        }
        impl->execution_error_event = (cflow_event_id)execution->id;
        impl->communication_error_event = (cflow_event_id)communication->id;
    }
    impl->bindings = build.bindings;
    impl->binding_count = build.executable_index;
    impl->guard_bindings = build.guard_bindings;
    impl->guard_users = build.guard_users;
    impl->guard_binding_count = build.guard_index;
    impl->blocks = build.blocks;
    impl->steps = build.steps;
    impl->branches = build.branches;
    impl->branch_count = build.branch_index;
    impl->effects = build.effects;
    impl->effect_count = build.effect_index;
    impl->payloads = build.payloads;
    impl->payload_count = build.payload_index;
    impl->max_payload_entries = counts.max_payload_entries;
    impl->assignments = build.assignments;
    impl->assignment_count = build.assignment_index;
    impl->data_initializer_count = counts.late_initializer_rows != 0u
        ? 0u : counts.data_initializer_rows;
    impl->late_initializer_count = counts.late_initializer_rows;
    impl->foreach_descriptors = build.foreach_descriptors;
    impl->foreach_count = build.foreach_index;
    impl->invocations = build.invocations;
    impl->invocation_count = build.invocation_index;
    impl->done_data = build.done_data;
    impl->done_data_count = build.done_data_index;
    impl->log_storage = build.log_storage;
    impl->effect_storage = build.effect_storage;
    impl->invocation_storage = build.invocation_storage;
    for (index = 0u; index < impl->guard_binding_count; ++index) {
        impl->guard_users[index].event_names_by_id =
            impl->event_names_by_id;
        impl->guard_users[index].event_name_count = impl->event_name_count;
        impl->guard_users[index].system_values.name =
            (scxml_expr_string_view){
                impl->document_name, impl->document_name_size};
    }
    for (index = 0u; index < impl->binding_count; ++index) {
        scxml_block *block =
            (scxml_block *)impl->bindings[index].user;
        if (block != NULL) {
            block->event_names_by_id = impl->event_names_by_id;
            block->event_name_count = impl->event_name_count;
            block->system_values.name =
                (scxml_expr_string_view){
                    impl->document_name, impl->document_name_size};
        }
    }
    build.bindings = NULL;
    build.guard_bindings = NULL;
    build.guard_users = NULL;
    build.blocks = NULL;
    build.steps = NULL;
    build.branches = NULL;
    build.effects = NULL;
    build.payloads = NULL;
    build.assignments = NULL;
    build.foreach_descriptors = NULL;
    build.invocations = NULL;
    build.done_data = NULL;
    build.log_storage = NULL;
    build.effect_storage = NULL;
    build.invocation_storage = NULL;
    impl->null_value = false;
    qsort(impl->state_names, impl->state_name_count,
          sizeof(*impl->state_names), scxml_analyze_compare_program_name);
    qsort(impl->event_names, impl->event_name_count,
          sizeof(*impl->event_names), scxml_analyze_compare_program_name);
    for (index = 0u; index < impl->event_name_count; ++index) {
        const uint64_t id = impl->event_names[index].id;
        if (id == 0u || id > impl->event_name_count ||
            impl->event_names_by_id[id - 1u] != NULL) {
            status = scxml_analyze_fail(
                &build, SCXML_NATIVE_IR_REJECTED,
                scxml_syntax_node_location(root),
                "SCXML event ID map invariant failed");
            goto cleanup;
        }
        impl->event_names_by_id[id - 1u] = &impl->event_names[index];
    }
    out->impl = impl;
    impl = NULL;
    status = SCXML_OK;

cleanup:
    if (impl != NULL) {
        cflow_statechart_destroy(&impl->statechart);
        free(impl->state_names);
        free(impl->event_names);
        free(impl->event_names_by_id);
        free(impl->bindings);
        free(impl->guard_bindings);
        scxml_emit_destroy_guard_users(impl->guard_users, impl->guard_binding_count);
        free(impl->guard_users);
        free(impl->blocks);
        free(impl->steps);
        scxml_emit_destroy_branches(impl->branches, impl->branch_count);
        free(impl->branches);
        scxml_emit_destroy_effects(impl->effects, impl->effect_count);
        free(impl->effects);
        scxml_emit_destroy_payloads(impl->payloads, impl->payload_count);
        free(impl->payloads);
        scxml_emit_destroy_assignments(impl->assignments, impl->assignment_count);
        free(impl->assignments);
        free(impl->foreach_descriptors);
        scxml_emit_destroy_invocations(impl->invocations, impl->invocation_count);
        free(impl->invocations);
        scxml_emit_destroy_done_data(impl->done_data, impl->done_data_count);
        free(impl->done_data);
        free(impl->name_storage);
        free(impl->log_storage);
        free(impl->effect_storage);
        free(impl->invocation_storage);
        free(impl);
    }
    scxml_emit_free_build(&build);
    scxml_ast_destroy(&ast);
    turbo_xml_document_destroy(&document);
    return status;
}

static scxml_expr_limits expression_limits_from_options(
    const scxml_cmeta_compile_options_v1 *options) {
    const scxml_expr_limits limits = {
        options->max_source_bytes,
        options->max_instructions,
        options->max_operands,
        options->max_expression_depth,
        options->max_path_depth,
        options->max_literal_bytes,
        options->max_string_bytes
    };
    return limits;
}

static bool cmeta_state_type_supported(const cmeta_type_desc *type) {
    return cmeta_type_require_traits(
               type,
               CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) ==
               CMETA_OK ||
           cmeta_type_require_traits(
               type,
               CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                   CMETA_TRAIT_DESTROY) == CMETA_OK;
}

scxml_status scxml_compile(
    scxml_program *out, const char *input, size_t input_size,
    const scxml_limits *limits,
    scxml_diagnostic *diagnostic) {
    return compile_scxml_model(
        out, input, input_size, limits, SCXML_DATA_MODEL_NULL, NULL, NULL,
        0u, diagnostic);
}

scxml_status scxml_compile_cmeta(
    scxml_program *out, const char *input, size_t input_size,
    const scxml_limits *limits,
    const scxml_cmeta_compile_options_v1 *options,
    scxml_diagnostic *diagnostic) {
    const size_t legacy_size =
        offsetof(scxml_cmeta_compile_options_v1, max_iterations);
    bool has_current_tail;
    size_t max_iterations;
    scxml_expr_limits expression_limits;
    has_current_tail = options != NULL &&
        options->struct_size >= sizeof(*options);
    if (options == NULL ||
        options->abi_version !=
            SCXML_CMETA_COMPILE_OPTIONS_ABI_V1 ||
        (options->struct_size != legacy_size && !has_current_tail) ||
        !cmeta_data_desc_valid(options->root) ||
        options->root->kind != CMETA_DATA_STRUCT ||
        options->root->storage_type == NULL ||
        !cmeta_type_desc_valid(options->root->storage_type) ||
        !cmeta_state_type_supported(options->root->storage_type)) {
        if (diagnostic != NULL) {
            memset(diagnostic, 0, sizeof(*diagnostic));
            diagnostic->status = SCXML_INVALID_ARGUMENT;
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", "invalid CMeta compile provider");
        }
        return SCXML_INVALID_ARGUMENT;
    }
    max_iterations = has_current_tail
        ? options->max_iterations
        : SCXML_CMETA_DEFAULT_MAX_ITERATIONS;
    expression_limits = expression_limits_from_options(options);
    if (!scxml_expr_limits_valid(&expression_limits) ||
        max_iterations == 0u) {
        if (diagnostic != NULL) {
            memset(diagnostic, 0, sizeof(*diagnostic));
            diagnostic->status = SCXML_INVALID_ARGUMENT;
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", "invalid SCXML expression limits");
        }
        return SCXML_INVALID_ARGUMENT;
    }
    return compile_scxml_model(
        out, input, input_size, limits, SCXML_DATA_MODEL_CMETA,
        options->root, &expression_limits, max_iterations, diagnostic);
}

void scxml_program_destroy(scxml_program *program) {
    scxml_program_impl *impl;
    if (program == NULL || program->impl == NULL) return;
    impl = (scxml_program_impl *)program->impl;
    cflow_statechart_destroy(&impl->statechart);
    free(impl->state_names);
    free(impl->event_names);
    free(impl->event_names_by_id);
    free(impl->bindings);
    free(impl->guard_bindings);
    scxml_emit_destroy_guard_users(impl->guard_users, impl->guard_binding_count);
    free(impl->guard_users);
    free(impl->blocks);
    free(impl->steps);
    scxml_emit_destroy_branches(impl->branches, impl->branch_count);
    free(impl->branches);
    scxml_emit_destroy_effects(impl->effects, impl->effect_count);
    free(impl->effects);
    scxml_emit_destroy_payloads(impl->payloads, impl->payload_count);
    free(impl->payloads);
    scxml_emit_destroy_assignments(impl->assignments, impl->assignment_count);
    free(impl->assignments);
    free(impl->foreach_descriptors);
    scxml_emit_destroy_invocations(impl->invocations, impl->invocation_count);
    free(impl->invocations);
    scxml_emit_destroy_done_data(impl->done_data, impl->done_data_count);
    free(impl->done_data);
    free(impl->name_storage);
    free(impl->log_storage);
    free(impl->effect_storage);
    free(impl->invocation_storage);
    free(impl);
    program->impl = NULL;
}

const cflow_statechart *scxml_program_statechart(
    const scxml_program *program) {
    const scxml_program_impl *impl =
        program != NULL ? (const scxml_program_impl *)program->impl : NULL;
    return impl != NULL ? &impl->statechart : NULL;
}

const scxml_program_name *scxml_program_find_name(
    const scxml_program_name *names, size_t count,
    const char *name, size_t name_size) {
    size_t low = 0u;
    size_t high = count;
    const turbo_xml_string_view wanted = {name, name_size};
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const turbo_xml_string_view value = {
            names[middle].name, names[middle].size};
        if (scxml_analyze_compare_view(value, wanted) < 0) low = middle + 1u;
        else high = middle;
    }
    return low < count && names[low].size == name_size &&
                   memcmp(names[low].name, name, name_size) == 0
               ? &names[low]
               : NULL;
}

bool scxml_program_state_id(const scxml_program *program,
                                  const char *name, size_t name_size,
                                  cflow_machine_state_id *out_id) {
    const scxml_program_impl *impl;
    const scxml_program_name *found;
    if (program == NULL || program->impl == NULL || name == NULL ||
        name_size == 0u || out_id == NULL) return false;
    impl = (const scxml_program_impl *)program->impl;
    found = scxml_program_find_name(impl->state_names, impl->state_name_count,
                              name, name_size);
    if (found == NULL) return false;
    *out_id = (cflow_machine_state_id)found->id;
    return true;
}

bool scxml_program_event_id(const scxml_program *program,
                                  const char *name, size_t name_size,
                                  cflow_event_id *out_id) {
    const scxml_program_impl *impl;
    const scxml_program_name *found;
    if (program == NULL || program->impl == NULL || name == NULL ||
        name_size == 0u || out_id == NULL) return false;
    impl = (const scxml_program_impl *)program->impl;
    found = scxml_program_find_name(impl->event_names, impl->event_name_count,
                              name, name_size);
    if (found == NULL) return false;
    *out_id = (cflow_event_id)found->id;
    return true;
}

const void *scxml_program_initial_state(
    const scxml_program *program) {
    const scxml_program_impl *impl =
        program != NULL ? (const scxml_program_impl *)program->impl : NULL;
    return impl != NULL && impl->data_model == SCXML_DATA_MODEL_NULL
               ? &impl->null_value
               : NULL;
}

bool scxml_program_event(const scxml_program *program,
                               const char *name, size_t name_size,
                               cflow_event_view *out_event) {
    const scxml_program_impl *impl;
    cflow_event_id id;
    if (out_event == NULL ||
        !scxml_program_event_id(program, name, name_size, &id)) {
        return false;
    }
    impl = (const scxml_program_impl *)program->impl;
    *out_event = (cflow_event_view){id, &cmeta_type_bool, &impl->null_value};
    return true;
}

bool scxml_program_requirements(
    const scxml_program *program, uint32_t *out_requirements) {
    const scxml_program_impl *impl = program != NULL
        ? (const scxml_program_impl *)program->impl : NULL;
    if (impl == NULL || out_requirements == NULL) return false;
    *out_requirements = impl->requirements;
    return true;
}

bool scxml_program_instance_bindings(
    const scxml_program *program,
    const cflow_statechart_executable_binding **out_bindings,
    size_t *out_count) {
    if (program == NULL || program->impl == NULL || out_bindings == NULL ||
        out_count == NULL) {
        return false;
    }
    {
        const scxml_program_impl *impl =
            (const scxml_program_impl *)program->impl;
        if (impl->requirements != SCXML_REQUIREMENT_NONE)
            return false;
        *out_bindings = impl->bindings;
        *out_count = impl->binding_count;
    }
    return true;
}

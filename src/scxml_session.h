#ifndef SCXML_SESSION_H
#define SCXML_SESSION_H

#include "scxml_impl.h"

bool scxml_session_data_initializer_is_overridden(
    const scxml_session_impl *session, size_t assignment);

cflow_statechart_instance_status scxml_session_init_quickjs_model(
    scxml_session *session, const scxml_session_config *config,
    const scxml_quickjs_session_options_v1 *options);

#endif

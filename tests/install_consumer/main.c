#include <cflow/scxml.h>

int main(void) {
    const cflow_scxml_limits limits = cflow_scxml_default_limits();
    return limits.max_states > 0u && limits.max_events > 0u &&
                   limits.max_transitions > 0u && limits.max_name_bytes > 0u
               ? 0
               : 1;
}

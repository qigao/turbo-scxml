#include <scxml/scxml.h>

int main(void) {
    const scxml_limits limits = scxml_default_limits();
    return limits.max_states > 0u && limits.max_events > 0u &&
                   limits.max_transitions > 0u && limits.max_name_bytes > 0u
               ? 0
               : 1;
}

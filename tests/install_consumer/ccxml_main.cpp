#include <ccxml/ccxml.h>

int main() {
    ccxml_session session{};
    const ccxml_limits limits = ccxml_default_limits();
    const bool valid = limits.max_transitions > 0u &&
                       limits.max_actions > 0u &&
                       limits.max_name_bytes > 0u && session.impl == nullptr;
    return valid ? 0 : 1;
}

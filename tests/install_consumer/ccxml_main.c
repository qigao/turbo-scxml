#include <ccxml/ccxml.h>

int main(void) {
    ccxml_program program = {0};
    const ccxml_limits limits = ccxml_default_limits();
    const int valid =
        limits.max_transitions > 0u && limits.max_actions > 0u &&
        limits.max_name_bytes > 0u &&
        CCXML_TELEPHONY_ADAPTER_ABI_V1 == 1u;
    ccxml_program_destroy(&program);
    return valid ? 0 : 1;
}

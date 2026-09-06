#include "voicexml_cmeta_fixture.h"

static cmeta_type_desc peer_int_type;
static cmeta_data_desc peer_int_data;
static bool peer_initialized;

const cmeta_data_desc *turboscxml_install_cmeta_peer_value_descriptor(void) {
    if (!peer_initialized) {
        peer_int_type = cmeta_type_int;
        peer_int_data = cmeta_data_int;
        peer_int_data.storage_type = &peer_int_type;
        peer_initialized = true;
    }
    return &peer_int_data;
}

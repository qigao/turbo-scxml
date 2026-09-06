#include "voicexml_cmeta_fixture.h"

#include <stddef.h>

static const cmeta_type_identity root_identity =
    CMETA_TYPE_ID_ATOM_INIT("turboscxml.install.voicexml.cmeta.root");
static const cmeta_type_traits root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static const cmeta_type_desc root_type = {
    .name = "turboscxml_install_cmeta_root",
    .size = sizeof(turboscxml_install_cmeta_root),
    .align = _Alignof(turboscxml_install_cmeta_root),
    .kind = CMETA_T_OBJECT,
    .traits = &root_traits,
    .identity = &root_identity
};
static cmeta_data_field_desc root_fields[] = {
    {"turboscxml.install.voicexml.cmeta.root.value", "value",
     offsetof(turboscxml_install_cmeta_root, value), NULL}
};
static const cmeta_data_struct_shape root_shape = {
    .layout = StructMeta(turboscxml_install_cmeta_root),
    .fields = root_fields,
    .field_count = 1u
};
static const cmeta_data_desc root_descriptor = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "turboscxml.install.voicexml.cmeta.root.data",
    .display_name = "TurboSCXML installed VoiceXML CMeta root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &root_type,
    .shape = &root_shape
};

const cmeta_data_desc *turboscxml_install_cmeta_root_descriptor(void) {
    root_fields[0].value = turboscxml_install_cmeta_peer_value_descriptor();
    return &root_descriptor;
}

const cmeta_type_desc *turboscxml_install_cmeta_layout_value_type(void) {
    return root_shape.layout->fields[0].type;
}

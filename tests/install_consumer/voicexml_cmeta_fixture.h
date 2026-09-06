#ifndef TURBOSCXML_INSTALL_CONSUMER_VOICEXML_CMETA_FIXTURE_H
#define TURBOSCXML_INSTALL_CONSUMER_VOICEXML_CMETA_FIXTURE_H

#include <voicexml/cmeta.h>

Struct(turboscxml_install_cmeta_root,
    (int, value)
);

#ifdef __cplusplus
extern "C" {
#endif

const cmeta_data_desc *turboscxml_install_cmeta_root_descriptor(void);
const cmeta_type_desc *turboscxml_install_cmeta_layout_value_type(void);
const cmeta_data_desc *turboscxml_install_cmeta_peer_value_descriptor(void);

#ifdef __cplusplus
}
#endif

#endif

#include <scxml/cnet_ingress.h>
#include <salts/error_codes.h>

int main() {
    scxml_cnet_ingress ingress = {};
    scxml_cnet_ingress_config config = {};
    if (scxml_cnet_ingress_init(&ingress, &config) != SALTS_EINVAL)
        return 1;
    return scxml_cnet_ingress_destroy(&ingress) == SALTS_OK ? 0 : 2;
}

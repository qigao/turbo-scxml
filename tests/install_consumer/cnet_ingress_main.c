#include <scxml/cnet_ingress.h>
#include <salts/error_codes.h>

int main(void) {
    scxml_cnet_ingress ingress = {0};
    scxml_cnet_ingress_stats stats = {0};
    if (scxml_cnet_ingress_get_stats(&ingress, &stats)) return 1;
    if (scxml_cnet_ingress_arm(&ingress) != SALTS_EINVAL) return 2;
    return scxml_cnet_ingress_destroy(&ingress) == SALTS_OK ? 0 : 3;
}

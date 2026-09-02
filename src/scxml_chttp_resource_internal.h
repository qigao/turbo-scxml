#ifndef SCXML_CHTTP_RESOURCE_INTERNAL_H
#define SCXML_CHTTP_RESOURCE_INTERNAL_H

#include <scxml/chttp_resource.h>

typedef struct scxml_chttp_transport_v1 {
    int (*get)(
        void *user, chttp_client *client, const chttp_options *options,
        chttp_response *out_response, chttp_error *out_error);
    void (*response_destroy)(void *user, chttp_response *response);
} scxml_chttp_transport_v1;

scxml_status scxml_chttp_resource_set_transport_for_test(
    scxml_chttp_resource *resource,
    const scxml_chttp_transport_v1 *transport,
    void *transport_user);

#endif /* SCXML_CHTTP_RESOURCE_INTERNAL_H */

#include <scxml/chttp_event_io.h>
#include <scxml/scxml.h>

int main(void) {
    scxml_chttp_processor processor = {0};
    scxml_chttp_binding binding = {0};
    int (*processor_init)(
        scxml_chttp_processor *,
        const scxml_chttp_processor_config_v1 *) =
        scxml_chttp_processor_init;
    int (*processor_start)(scxml_chttp_processor *) =
        scxml_chttp_processor_start;
    int (*processor_stop)(scxml_chttp_processor *, uint32_t) =
        scxml_chttp_processor_stop;
    int (*processor_destroy)(scxml_chttp_processor *) =
        scxml_chttp_processor_destroy;
    int (*binding_init)(
        scxml_chttp_binding *, scxml_chttp_processor *,
        const scxml_chttp_binding_config_v1 *) =
        scxml_chttp_binding_init;
    const scxml_event_io_adapter *(*adapter)(void) =
        scxml_chttp_event_io_adapter;
    void *(*adapter_user)(scxml_chttp_binding *) =
        scxml_chttp_binding_adapter_user;
    bool (*ioprocessor)(
        const scxml_chttp_binding *, scxml_ioprocessor_descriptor *) =
        scxml_chttp_binding_ioprocessor;
    int (*binding_activate)(
        scxml_chttp_binding *, scxml_session *, const scxml_program *) =
        scxml_chttp_binding_activate;
    int (*binding_destroy)(scxml_chttp_binding *) =
        scxml_chttp_binding_destroy;
    bool (*get_stats)(
        const scxml_chttp_processor *, scxml_chttp_processor_stats *) =
        scxml_chttp_processor_get_stats;

    return processor.impl == NULL && binding.impl == NULL &&
            processor_init != NULL && processor_start != NULL &&
            processor_stop != NULL && processor_destroy != NULL &&
            binding_init != NULL && adapter != NULL &&
            adapter_user != NULL && ioprocessor != NULL &&
            binding_activate != NULL && binding_destroy != NULL &&
            get_stats != NULL && SCXML_CHTTP_ABI_V1 == 1u
        ? 0 : 1;
}

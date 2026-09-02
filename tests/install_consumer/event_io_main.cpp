#include <scxml/scxml.h>
#include <scxml/chttp_event_io.h>

int main() {
    scxml_chttp_processor processor{};
    scxml_chttp_binding binding{};
    auto processor_init = &scxml_chttp_processor_init;
    auto processor_start = &scxml_chttp_processor_start;
    auto processor_stop = &scxml_chttp_processor_stop;
    auto processor_destroy = &scxml_chttp_processor_destroy;
    auto binding_init = &scxml_chttp_binding_init;
    auto binding_adapter = &scxml_chttp_binding_event_io_adapter;
    auto binding_user = &scxml_chttp_binding_adapter_user;
    auto binding_ioprocessor = &scxml_chttp_binding_ioprocessor;
    auto binding_activate = &scxml_chttp_binding_activate;
    auto binding_destroy = &scxml_chttp_binding_destroy;
    auto processor_stats = &scxml_chttp_processor_get_stats;

    return processor.impl == nullptr && binding.impl == nullptr &&
                   processor_init != nullptr && processor_start != nullptr &&
                   processor_stop != nullptr &&
                   processor_destroy != nullptr && binding_init != nullptr &&
                   binding_adapter != nullptr && binding_user != nullptr &&
                   binding_ioprocessor != nullptr &&
                   binding_activate != nullptr &&
                   binding_destroy != nullptr && processor_stats != nullptr
               ? 0
               : 1;
}

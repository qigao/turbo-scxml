#include <scxml/chttp_event_io.h>
#include <scxml/scxml.h>

int main() {
    scxml_chttp_processor processor{};
    scxml_chttp_binding binding{};
    const auto init = &scxml_chttp_processor_init;
    const auto adapter = &scxml_chttp_event_io_adapter;
    const auto stats = &scxml_chttp_processor_get_stats;
    return processor.impl == nullptr && binding.impl == nullptr &&
            init != nullptr && adapter != nullptr && stats != nullptr
        ? 0 : 1;
}

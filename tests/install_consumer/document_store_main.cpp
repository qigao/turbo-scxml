#include <voicexml/document_store.h>

#include <type_traits>

static_assert(std::is_standard_layout<vxml_document_store>::value,
              "document store handle must remain C-compatible");
static_assert(std::is_standard_layout<vxml_document_ref>::value,
              "document ref must remain C-compatible");
static_assert(std::is_standard_layout<vxml_document_store_config_v1>::value,
              "document store config must remain C-compatible");
static_assert(std::is_standard_layout<vxml_document_compile_adapter_v1>::value,
              "document compiler adapter must remain C-compatible");
static_assert(std::is_standard_layout<vxml_resolved_uri_v1>::value,
              "resolved URI record must remain C-compatible");

int main() {
    vxml_document_store_config_v1 config{};
    vxml_document_compile_adapter_v1 compiler{};
    vxml_document_store store{};
    auto compile_source_fn =
        &vxml_document_store_compile_source;
    config.compiler = &compiler;
    config.compiler_user = nullptr;
    return store.impl == nullptr &&
           config.compiler == &compiler &&
           compile_source_fn != nullptr ? 0 : 1;
}

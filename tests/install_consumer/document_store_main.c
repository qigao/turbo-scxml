#include <voicexml/document_store.h>

static vxml_status compile_document(
    void *user,
    const void *source, size_t source_size,
    vxml_program *out_program,
    vxml_diagnostic *diagnostic) {
    (void)user;
    (void)source;
    (void)source_size;
    (void)out_program;
    (void)diagnostic;
    return VXML_UNSUPPORTED_FEATURE;
}

int main(void) {
    const vxml_document_compile_adapter_v1 compiler = {
        .abi_version = VXML_DOCUMENT_COMPILE_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_document_compile_adapter_v1),
        .compile = compile_document};
    vxml_document_store_config_v1 config = {0};
    vxml_document_store store = {0};
    vxml_document_ref ref = {0};
    vxml_document_store_stats stats = {0};
    vxml_document_store_status (*compile_source_fn)(
        const vxml_document_store *,
        const void *, size_t,
        vxml_program *, vxml_diagnostic *) =
        vxml_document_store_compile_source;

    config.compiler = &compiler;
    config.compiler_user = NULL;
    if (config.compiler->compile == NULL ||
        compile_source_fn == NULL)
        return 1;
    if (store.impl != NULL)
        return 2;
    if (ref.slot != 0u || ref.generation != 0u)
        return 3;
    if (vxml_document_store_get_stats(&store, &stats))
        return 4;
    return 0;
}

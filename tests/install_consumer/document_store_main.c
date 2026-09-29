#include <voicexml/document_store.h>

int main(void) {
    vxml_document_store store = {0};
    vxml_document_ref ref = {0};
    vxml_document_store_stats stats = {0};

    if (store.impl != NULL)
        return 1;
    if (ref.slot != 0u || ref.generation != 0u)
        return 2;
    if (vxml_document_store_get_stats(&store, &stats))
        return 3;
    return 0;
}

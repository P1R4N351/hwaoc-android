/* Standalone probe for vk_tbuf_spv.h: probe <in.spv> <out.spv> <binding>... Prints the rewrite's result; writes
   out.spv only when bindings were retyped; exits 1 when the module is refused. No GPU, no game files. */
#include <stdio.h>
#include "vk_tbuf_spv.h"
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s in.spv out.spv binding...\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    if (fseek(f, 0, SEEK_END)) return 2;
    long size = ftell(f); if (size < 20 || size % 4 || fseek(f, 0, SEEK_SET)) return 2;
    size_t count = (size_t)size / 4; uint32_t *words = malloc((size_t)size);
    if (!words || fread(words, 4, count, f) != count) return 2;
    fclose(f);
    uint32_t *before = malloc((size_t)size); if (!before) return 2;
    memcpy(before, words, (size_t)size);
    uint32_t bindings[VK_TBUF_SPV_MAX_BINDINGS + 1]; int nb = 0;
    for (int i = 3; i < argc && nb <= VK_TBUF_SPV_MAX_BINDINGS; i++) bindings[nb++] = (uint32_t)strtoul(argv[i], NULL, 10);
    const uint32_t *held = words; size_t held_count = count;
    int result = vk_spirv_tbuf(&words, &count, bindings, nb);
    if (result <= 0 && (words != held || count != held_count || memcmp(words, before, (size_t)size))) {
        fprintf(stderr, "the module was changed although the result is %d\n", result); return 3;
    }
    printf("result %d words %zu\n", result, count);
    if (result > 0) { f = fopen(argv[2], "wb"); if (!f || fwrite(words, 4, count, f) != count || fclose(f)) return 2; }
    free(words); free(before);
    return result < 0;
}

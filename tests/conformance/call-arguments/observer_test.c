#include <stdio.h>
#include <stdlib.h>

#define main kofun_stage2_call_arguments_embedded_main
#define KOFUN_STAGE2_AUTHORITY_API 1
#include "../../../bootstrap/stage2/compiler.c"
#undef KOFUN_STAGE2_AUTHORITY_API
#undef main

static char *read_source(const char *path) {
    FILE *input = fopen(path, "rb");
    long length;
    char *source;
    if (input == NULL || fseek(input, 0, SEEK_END) != 0 ||
        (length = ftell(input)) < 0 || fseek(input, 0, SEEK_SET) != 0) {
        if (input != NULL) (void)fclose(input);
        return NULL;
    }
    source = malloc((size_t)length + 1u);
    if (source == NULL ||
        fread(source, 1u, (size_t)length, input) != (size_t)length ||
        fclose(input) != 0) {
        free(source);
        return NULL;
    }
    source[length] = '\0';
    return source;
}

int main(int argc, char **argv) {
    Stage2AuthorityContext context;
    Stage2AuthorityResult result;
    char *source;
    if ((argc != 2 && argc != 3 && argc != 4) ||
        (source = read_source(argv[1])) == NULL) return 2;
    if (!(argc == 3 && strcmp(argv[2], "ownership") == 0
            ? stage2_ownership_outcome(source, &context, &result)
            : stage2_compile_outcome(source, &context, &result))) {
        free(source);
        return 3;
    }
    if (argc == 4) {
        const char *directive = strstr(source, "# expect-span: byte ");
        int64_t expected_start = directive == NULL
            ? -1 : strtoll(directive + strlen("# expect-span: byte "),
                NULL, 10);
        bool expect_related = strcmp(argv[3], "-") != 0;
        bool related_matches = !expect_related ||
            (context.diagnostic.related_count == 1u &&
             strcmp(context.diagnostic.related[0].label,
                 "declared parameter") == 0 &&
             context.diagnostic.related[0].end -
                 context.diagnostic.related[0].start ==
                    (int64_t)strlen(argv[3]) &&
             memcmp(source + context.diagnostic.related[0].start,
                 argv[3], strlen(argv[3])) == 0);
        bool ok = result.exit_class == 1u &&
            context.diagnostic.present &&
            strcmp(context.diagnostic.code, argv[2]) == 0 &&
            context.diagnostic.start == expected_start &&
            context.diagnostic.affected_count == 1u &&
            context.diagnostic.affected[0].kind ==
                STAGE2_DIAGNOSTIC_AFFECTED_CALL &&
            ((expect_related && related_matches) ||
             (!expect_related && context.diagnostic.related_count == 0u));
        stage2_authority_result_destroy(&result);
        free(source);
        return ok ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[2], "scope") == 0) {
        if (result.scope_hir != NULL) fputs(result.scope_hir, stdout);
        stage2_authority_result_destroy(&result);
        free(source);
        return 0;
    }
    if (argc == 3 && strcmp(argv[2], "diagnostic") == 0) {
        if (result.diagnostic != NULL) fputs(result.diagnostic, stdout);
        stage2_authority_result_destroy(&result);
        free(source);
        return 0;
    }
    if (result.semantic_observations != NULL) {
        fputs(result.semantic_observations, stdout);
    }
    stage2_authority_result_destroy(&result);
    free(source);
    return 0;
}

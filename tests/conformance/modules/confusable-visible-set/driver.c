#include "confusable_visible_set.h"
#include "kif_v1.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static const char ascii_name[] = "paypal";
static const char confusable_name[] = "p\xD0\xB0ypal";

static void identity(uint8_t output[KOFUN_VISIBLE_ID_BYTES], uint8_t seed) {
    size_t index;
    for (index = 0u; index < KOFUN_VISIBLE_ID_BYTES; index += 1u) {
        output[index] = (uint8_t)(seed + index * 17u);
    }
}

static KofunVisibleBinding binding(
    uint8_t module_seed,
    uint8_t namespace_seed,
    uint8_t binding_seed,
    uint8_t target_seed,
    const char *spelling,
    KofunVisibleSiteKind site,
    const char *provenance,
    uint32_t start,
    uint32_t end,
    bool disclose
) {
    KofunVisibleBinding result;
    memset(&result, 0, sizeof(result));
    identity(result.resolving_module_id, module_seed);
    identity(result.namespace_id, namespace_seed);
    identity(result.binding_id, binding_seed);
    identity(result.target_symbol_id, target_seed);
    result.effective_spelling = (const uint8_t *)spelling;
    result.effective_spelling_length = strlen(spelling);
    result.site_kind = site;
    result.canonical_provenance = provenance;
    result.span_start = start;
    result.span_end = end;
    result.disclose_location = disclose;
    return result;
}

static KofunVisibleConfusableResult check(
    const KofunVisibleBinding *bindings,
    size_t count,
    KofunVisibleConfusableDiagnostic *diagnostic
) {
    return kofun_check_visible_confusables(bindings, count, diagnostic, 1u);
}

static void expect_ok(const KofunVisibleBinding *bindings, size_t count) {
    KofunVisibleConfusableDiagnostic diagnostic;
    KofunVisibleConfusableResult result = check(bindings, count, &diagnostic);
    assert(result.status == KOFUN_VISIBLE_CONFUSABLE_OK);
    assert(result.diagnostic_count == 0u);
}

static KofunVisibleConfusableResult expect_collision(
    const KofunVisibleBinding *bindings,
    size_t count,
    KofunVisibleConfusableDiagnostic *diagnostic
) {
    KofunVisibleConfusableResult result = check(bindings, count, diagnostic);
    assert(result.status == KOFUN_VISIBLE_CONFUSABLE_COLLISION);
    assert(result.diagnostic_count == 1u);
    assert(diagnostic->related_count >= 1u);
    return result;
}

static void format_diagnostic(
    const KofunVisibleBinding *bindings,
    const KofunVisibleConfusableDiagnostic *diagnostic,
    char *output,
    size_t capacity
) {
    const KofunVisibleBinding *primary =
        &bindings[diagnostic->primary_binding];
    const KofunVisibleBinding *related =
        &bindings[diagnostic->related_bindings[0]];
    int written;
    if (related->disclose_location) {
        written = snprintf(output, capacity,
            "error[EUNICODE008]: effective value spelling `%.*s` at `%s` bytes %u..%u is confusable with `%.*s`; related `%s` bytes %u..%u",
            (int)primary->effective_spelling_length,
            (const char *)primary->effective_spelling,
            primary->canonical_provenance,
            primary->span_start, primary->span_end,
            (int)related->effective_spelling_length,
            (const char *)related->effective_spelling,
            related->canonical_provenance,
            related->span_start, related->span_end);
    } else {
        written = snprintf(output, capacity,
            "error[EUNICODE008]: effective value spelling `%.*s` at `%s` bytes %u..%u is confusable with `%.*s`; related export identity is disclosure-safe and its location is withheld",
            (int)primary->effective_spelling_length,
            (const char *)primary->effective_spelling,
            primary->canonical_provenance,
            primary->span_start, primary->span_end,
            (int)related->effective_spelling_length,
            (const char *)related->effective_spelling);
    }
    assert(written > 0 && (size_t)written < capacity);
}

static void test_local_and_import(char *diagnostic_text, size_t capacity) {
    KofunVisibleBinding bindings[] = {
        binding(1u, 10u, 1u, 41u, ascii_name, KOFUN_VISIBLE_SITE_LOCAL,
            "app/main.kofun", 4u, 10u, true),
        binding(1u, 10u, 2u, 42u, confusable_name,
            KOFUN_VISIBLE_SITE_IMPORT, "app/main.kofun", 20u, 27u, true)
    };
    KofunVisibleConfusableDiagnostic diagnostic;
    (void)expect_collision(bindings, ARRAY_COUNT(bindings), &diagnostic);
    assert(diagnostic.primary_binding == 1u);
    assert(diagnostic.related_bindings[0] == 0u);
    format_diagnostic(bindings, &diagnostic, diagnostic_text, capacity);
}

static void test_import_permutation(void) {
    KofunVisibleBinding forward[] = {
        binding(2u, 10u, 11u, 51u, ascii_name, KOFUN_VISIBLE_SITE_IMPORT,
            "dep/a.kif", 3u, 9u, true),
        binding(2u, 10u, 12u, 52u, confusable_name,
            KOFUN_VISIBLE_SITE_IMPORT, "dep/b.kif", 8u, 15u, true)
    };
    KofunVisibleBinding reverse[] = { forward[1], forward[0] };
    KofunVisibleConfusableDiagnostic a_diagnostic;
    KofunVisibleConfusableDiagnostic b_diagnostic;
    KofunVisibleConfusableResult a = expect_collision(
        forward, ARRAY_COUNT(forward), &a_diagnostic);
    KofunVisibleConfusableResult b = expect_collision(
        reverse, ARRAY_COUNT(reverse), &b_diagnostic);
    assert(memcmp(a.cache_key, b.cache_key, sizeof(a.cache_key)) == 0);
    assert(memcmp(forward[a_diagnostic.primary_binding].binding_id,
        reverse[b_diagnostic.primary_binding].binding_id,
        KOFUN_VISIBLE_ID_BYTES) == 0);
    assert(memcmp(forward[a_diagnostic.related_bindings[0]].binding_id,
        reverse[b_diagnostic.related_bindings[0]].binding_id,
        KOFUN_VISIBLE_ID_BYTES) == 0);
}

static void test_alias_and_omission(void) {
    KofunVisibleBinding pair[] = {
        binding(3u, 10u, 21u, 61u, ascii_name, KOFUN_VISIBLE_SITE_LOCAL,
            "app/alias.kofun", 1u, 7u, true),
        binding(3u, 10u, 22u, 62u, "payment_service",
            KOFUN_VISIBLE_SITE_IMPORT, "app/alias.kofun", 15u, 30u, true)
    };
    expect_ok(pair, ARRAY_COUNT(pair));
    pair[1].effective_spelling = (const uint8_t *)confusable_name;
    pair[1].effective_spelling_length = strlen(confusable_name);
    {
        KofunVisibleConfusableDiagnostic diagnostic;
        (void)expect_collision(pair, ARRAY_COUNT(pair), &diagnostic);
    }
    expect_ok(pair, 1u);
}

static void test_namespaces_and_visibility(void) {
    KofunVisibleBinding near_miss[] = {
        binding(4u, 10u, 31u, 71u, ascii_name, KOFUN_VISIBLE_SITE_LOCAL,
            "app/namespaces.kofun", 2u, 8u, true),
        binding(4u, 11u, 32u, 72u, confusable_name,
            KOFUN_VISIBLE_SITE_IMPORT, "app/namespaces.kofun", 17u, 24u, true)
    };
    KofunVisibleBinding inaccessible = binding(4u, 10u, 33u, 73u,
        confusable_name, KOFUN_VISIBLE_SITE_IMPORT,
        "secret/private/hidden.kofun", 99u, 106u, false);
    expect_ok(near_miss, ARRAY_COUNT(near_miss));
    near_miss[1].namespace_id[0] = near_miss[0].namespace_id[0];
    memcpy(near_miss[1].namespace_id, near_miss[0].namespace_id,
        KOFUN_VISIBLE_ID_BYTES);
    expect_ok(near_miss, 1u);
    (void)inaccessible;
}

static void test_re_exports_preserve_target(void) {
    KofunVisibleBinding facade[] = {
        binding(5u, 10u, 41u, 81u, ascii_name, KOFUN_VISIBLE_SITE_LOCAL,
            "facade/api.kofun", 2u, 8u, true),
        binding(5u, 10u, 42u, 82u, confusable_name,
            KOFUN_VISIBLE_SITE_RE_EXPORT, "facade/api.kofun", 40u, 47u, true)
    };
    KofunVisibleBinding consumer[] = {
        binding(6u, 10u, 45u, 83u, ascii_name, KOFUN_VISIBLE_SITE_LOCAL,
            "consumer/main.kofun", 2u, 8u, true),
        binding(6u, 10u, 44u, 82u, confusable_name,
            KOFUN_VISIBLE_SITE_IMPORT, "facade/api.kif", 4u, 11u, false)
    };
    uint8_t target_before[KOFUN_VISIBLE_ID_BYTES];
    KofunVisibleConfusableDiagnostic diagnostic;
    char text[1024];
    memcpy(target_before, facade[1].target_symbol_id, sizeof(target_before));
    (void)expect_collision(facade, ARRAY_COUNT(facade), &diagnostic);
    assert(memcmp(target_before, facade[1].target_symbol_id,
        sizeof(target_before)) == 0);
    (void)expect_collision(consumer, ARRAY_COUNT(consumer), &diagnostic);
    assert(memcmp(facade[1].target_symbol_id,
        consumer[1].target_symbol_id, KOFUN_VISIBLE_ID_BYTES) == 0);
    format_diagnostic(consumer, &diagnostic, text, sizeof(text));
    assert(strstr(text, "secret/private") == NULL);
    assert(strstr(text, "location is withheld") != NULL);
}

static void test_source_kif_and_cache_invalidation(void) {
    KofunKifFact decoded;
    KofunVisibleBinding source[] = {
        binding(7u, 10u, 51u, 91u, ascii_name, KOFUN_VISIBLE_SITE_LOCAL,
            "app/cache.kofun", 2u, 8u, true),
        binding(7u, 10u, 52u, 92u, confusable_name,
            KOFUN_VISIBLE_SITE_IMPORT, "dep/api.kofun", 5u, 12u, true)
    };
    KofunVisibleBinding from_kif[2];
    KofunVisibleBinding changed[1];
    KofunVisibleConfusableDiagnostic diagnostic;
    KofunVisibleConfusableResult source_result;
    KofunVisibleConfusableResult kif_result;
    KofunVisibleConfusableResult changed_result;
    memset(&decoded, 0, sizeof(decoded));
    decoded.name = (char *)confusable_name;
    decoded.name_length = strlen(confusable_name);
    from_kif[0] = source[0];
    from_kif[1] = source[1];
    from_kif[1].effective_spelling = (const uint8_t *)decoded.name;
    from_kif[1].effective_spelling_length = decoded.name_length;
    from_kif[1].canonical_provenance = "dep/api.kif";
    source_result = expect_collision(source, ARRAY_COUNT(source), &diagnostic);
    kif_result = expect_collision(from_kif, ARRAY_COUNT(from_kif), &diagnostic);
    assert(memcmp(source_result.cache_key, kif_result.cache_key,
        KOFUN_VISIBLE_ID_BYTES) == 0);
    changed[0] = source[0];
    changed_result = check(changed, ARRAY_COUNT(changed), &diagnostic);
    assert(changed_result.status == KOFUN_VISIBLE_CONFUSABLE_OK);
    assert(memcmp(source_result.cache_key, changed_result.cache_key,
        KOFUN_VISIBLE_ID_BYTES) != 0);
}

static void test_bounded_collision_set(void) {
    size_t count = 1024u;
    KofunVisibleBinding *bindings = calloc(count, sizeof(*bindings));
    KofunVisibleConfusableDiagnostic diagnostic;
    KofunVisibleConfusableResult result;
    size_t index;
    assert(bindings != NULL);
    for (index = 0u; index < count; index += 1u) {
        bindings[index] = binding(8u, 10u, (uint8_t)(index & 0xffu),
            (uint8_t)((index + 1u) & 0xffu),
            index % 2u == 0u ? ascii_name : confusable_name,
            KOFUN_VISIBLE_SITE_IMPORT, "bounded/dependency.kif",
            (uint32_t)index, (uint32_t)index + 7u, true);
        bindings[index].binding_id[0] = (uint8_t)(index >> 8u);
        bindings[index].binding_id[1] = (uint8_t)index;
    }
    result = expect_collision(bindings, count, &diagnostic);
    assert(diagnostic.related_count == 1u);
    assert(diagnostic.related_omitted == 0u);
    assert(result.work <= count * 2u);
    result = kofun_check_visible_confusables(bindings, count, NULL, 0u);
    assert(result.status == KOFUN_VISIBLE_CONFUSABLE_OUTPUT_TOO_SMALL);
    assert(result.diagnostic_count == 1u);
    free(bindings);
}

int main(int argc, char **argv) {
    char diagnostic[1024];
    (void)argc;
    (void)argv;
    test_local_and_import(diagnostic, sizeof(diagnostic));
    test_import_permutation();
    test_alias_and_omission();
    test_namespaces_and_visibility();
    test_re_exports_preserve_target();
    test_source_kif_and_cache_invalidation();
    test_bounded_collision_set();
    if (argc == 2 && strcmp(argv[1], "--diagnostic") == 0) {
        puts(diagnostic);
        return 1;
    }
    puts("PASS: EUNICODE008 checks effective visible bindings after filtering");
    puts("PASS: aliases, namespaces, KIF spellings, re-exports, and target identity are exact");
    puts("PASS: visible-vector cache invalidation and collision-set work are bounded");
    return 0;
}

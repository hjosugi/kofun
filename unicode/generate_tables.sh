#!/usr/bin/env sh
set -eu

if test "$#" -ne 3; then
    printf '%s\n' \
        "usage: generate_tables.sh DerivedCoreProperties.txt confusables.txt OUTPUT.inc" >&2
    exit 2
fi

derived=$1
confusables=$2
output=$3

test -f "$derived"
test -f "$confusables"

temporary="${output}.tmp.$$"
trap 'rm -f "$temporary" "${temporary}.confusables"' 0 1 2 15

{
    printf '%s\n' \
        '/* Generated from Unicode 17.0.0 DerivedCoreProperties.txt and confusables.txt. */' \
        '/* Regenerate with unicode/generate_tables.sh; do not edit by hand. */' \
        ''

    for property in XID_Start XID_Continue; do
        case $property in
            XID_Start) table=kofun_xid_start_ranges ;;
            XID_Continue) table=kofun_xid_continue_ranges ;;
        esac
        printf 'static const KofunUnicodeRange %s[] = {\n' "$table"
        awk -F';' -v property="$property" '
            {
                value = $2
                sub(/#.*/, "", value)
                gsub(/^[ \t]+|[ \t]+$/, "", value)
                if (value != property) next
                range = $1
                gsub(/[ \t]/, "", range)
                count = split(range, ends, /\.\./)
                if (count == 1) ends[2] = ends[1]
                printf "    {UINT32_C(0x%s), UINT32_C(0x%s)},\n", ends[1], ends[2]
            }
        ' "$derived"
        printf '%s\n' '};' ''
    done
} >"$temporary"

awk -F';' '
    !/^#/ && NF >= 3 {
        source = $1
        target = $2
        gsub(/^[ \t]+|[ \t]+$/, "", source)
        gsub(/^[ \t]+|[ \t]+$/, "", target)
        key = substr("000000", 1, 6 - length(source)) source
        print key ";" source ";" target
    }
' "$confusables" |
    LC_ALL=C sort -t';' -k1,1 >"${temporary}.confusables"

awk -F';' '
    {
        sources[NR] = $2
        offsets[NR] = value_count
        lens[NR] = split($3, target, /[ \t]+/)
        for (item = 1; item <= lens[NR]; ++item) {
            values[value_count++] = target[item]
        }
        entries = NR
    }
    END {
        print "static const uint32_t kofun_confusable_values[] = {"
        for (item = 0; item < value_count; ++item) {
            printf "    UINT32_C(0x%s),\n", values[item]
        }
        print "};"
        print ""
        print "static const KofunConfusableMapping kofun_confusable_mappings[] = {"
        for (item = 1; item <= entries; ++item) {
            printf "    {UINT32_C(0x%s), UINT32_C(%d), UINT8_C(%d)},\n", \
                sources[item], offsets[item], lens[item]
        }
        print "};"
    }
' "${temporary}.confusables" >>"$temporary"

mv "$temporary" "$output"
rm -f "${temporary}.confusables"
trap - 0 1 2 15

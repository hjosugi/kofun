function rotate_cycle(start,    i, position, result) {
    result = ""
    for (i = 0; i < field_count; i++) {
        position = ((start - 1 + i) % field_count) + 1
        result = result (i == 0 ? "" : " ") fields[position]
    }
    return result
}

NF > 0 {
    field_count = NF
    for (i = 1; i <= NF; i++) fields[i] = $i
    minimum = 1
    for (i = 2; i <= NF; i++) {
        if (fields[i] < fields[minimum]) minimum = i
    }
    candidate = rotate_cycle(minimum)
    if (!have_best || field_count < best_count ||
        (field_count == best_count && candidate < best)) {
        best = candidate
        best_count = field_count
        have_best = 1
    }
}

END {
    if (!have_best) exit 1
    print best
}

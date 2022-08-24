// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * A class that represents the Disjoint Sets data structure.
 *
 *
 *//*
 * Authors:
 * Osama Ahmad
 *
 * Copyright (C) 2021 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "disjoint-sets.h"

void DisjointSets::merge(int a, int b)
{
    int parent = parent_of(a);
    int child = parent_of(b);

    if (child == parent) { return; }

    int parent_size = size_of_set(parent);
    int child_size = size_of_set(child);

    if (parent_size < child_size) {
        std::swap(parent, child);
    }

    parents[child] = parent;
    parents[parent] = -(parent_size + child_size);
}

int DisjointSets::parent_of(int x)
{
    if (parents[x] < 0) {
        return x;
    }

    int parent = parents[x];
    parents[x] = parent_of(parent);
    return parents[x];
}

int DisjointSets::size_of_set(int x)
{
    int parent = parent_of(x);
    return -parents[parent];
}

int DisjointSets::sets_count()
{
    int n = parents.size();
    std::vector<bool> is_present(n, false);

    int result = 0;

    for (int i = 0; i < n; i++) {
        int parent = parent_of(i);
        if (!is_present[parent]) {
            is_present[parent] = true;
            result++;
        }
    }

    return result;
}
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

#pragma once

#include <vector>

class DisjointSets
{
    // if parents[x] is negative, then x is a parent of itself,
    // and the negative number represents the size of the set.
    // else if parents[x] is positive, then x has a parent, parents[x]
    // might not be the top parent, thus shouldn't access parents[x]
    // directly, rather, use the method "parent_of".
    std::vector<int> parents;
public:
    DisjointSets(int n) { parents.resize(n, -1); }
    void merge(int a, int b);
    int parent_of(int x);
    int size_of_set(int x);
    int sets_count();
};
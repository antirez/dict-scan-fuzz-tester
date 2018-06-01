/* This program checks that the dictScan() algorithm is correct by
 * fuzzing it and checking that the hash slots were all visited.
 *
 * Copyright (C) 2018 Salvatore Sanfilippo <antirez@gmail.com>
 * Under the BSD 3 clause license. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define FIXED /* Use the fixed version of dictScan(). */

typedef struct {
    unsigned long size;
    unsigned long sizemask;
    int visited[1024];
} dictht;

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
static unsigned long rev(unsigned long v) {
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

unsigned long dictScan(dictht *t0, dictht *t1,
                       unsigned long v, int verbose)
{
    unsigned long m0, m1;

    if (t1 == NULL) {
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        if (verbose) printf("Single[%lu]\n", v&m0);
        t0->visited[v&m0] = 1;

#ifdef FIXED
        /* Set unmasked bits so incrementing the reversed cursor
         * operates on the masked bits of the smaller table */
        v |= ~m0;

        /* Increment the reverse cursor */
        v = rev(v);
        v++;
        v = rev(v);
#endif
    } else {
        /* Make sure t0 is the smaller and t1 is the bigger table */
        if (t0->size > t1->size) {
            dictht *aux = t1;
            t1 = t0;
            t0 = aux;
        }

        m0 = t0->sizemask;
        m1 = t1->sizemask;

        /* Emit entries at cursor */
        if (verbose) printf("Small[%lu]\n", v&m0);
        t0->visited[v&m0] = 1;

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        do {
            /* Emit entries at cursor */
            if (verbose) printf("Big[%lu]\n", v&m1);
            t1->visited[v&m1] = 1;

#ifdef FIXED
            /* Increment the reverse cursor not covered by the smaller mask.*/
            v |= ~m1;
            v = rev(v);
            v++;
            v = rev(v);
#else
            /* Increment bits not covered by the smaller mask */
            v = (((v | m0) + 1) & ~m0) | (v & m0);
#endif

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

#ifndef FIXED
    /* Set unmasked bits so incrementing the reversed cursor
     * operates on the masked bits of the smaller table */
    v |= ~m0;

    /* Increment the reverse cursor */
    v = rev(v);
    v++;
    v = rev(v);
#endif

    return v;
}

int check(dictht *t, char *name) {
    unsigned long i;
    int errors = 0;
    printf("Checking table %s of size %lu\n", name, t->size);
    for (i = 0; i < t->size; i++) {
        if (t->visited[i] == 0) {
            printf("Bucket %lu not visited!\n",i);
            errors++;
        }
    }
    return errors;
}

/* When a new hash table is added, we need to mark as visited all
 * the buckets in the new table that, in theory, do not require to be
 * vesited. When the new table is bigger, those are the expansion of
 * the old buckets to the new table. When the new table is instead smaller
 * we can mark only the buckets that have their expansion in the old
 * (bigger) table, all already marked as visited. */
void expand(dictht *old, dictht *new) {
    if (old->size <= new->size) {
        for (unsigned long i = 0; i < old->size; i++) {
            if (old->visited[i]) {
                for (unsigned long j = i; j < new->size; j += old->size) {
                    new->visited[j] = 1;
                }
            }
        }
    } else {
        for (unsigned long i = 0; i < new->size; i++) {
            unsigned long j;
            for (j = i; j < old->size; j += new->size) {
                if (!old->visited[j]) break;
            }
            if (j >= old->size) {
                /* All the expansions are already visited in the old
                 * table, so we can mark the new table bucket as visited. */
                new->visited[i] = 1;
            }
        }
    }
}

/* Simulate a SCAN with a rehashing.
 * Return 0 if no problem was found. */
int test_scan(dictht *t0, dictht *t1, int verbose) {
    unsigned long cursor = 0;
    int iteration = 0;
    int before_rehashing = 1;
    int first_rehashing_step = 1;
    do {
        if (before_rehashing) {
            cursor = dictScan(t0, NULL, cursor, verbose);
            if (!(rand() % t0->size)) {
                before_rehashing = 0;
                printf("Rehashing to new table: %lu -> %lu\n",
                    t0->size, t1->size);
            }
        } else {
            if (first_rehashing_step) {
                expand(t0,t1);
                first_rehashing_step = 0;
            }
            cursor = dictScan(t0, t1, cursor, verbose);
        }
        if (verbose) printf("cursor %lu\n", cursor);
        iteration++;
    } while (cursor != 0);

    /* Check that the two tables were visited. */
    if (check(t0,"table 0")) return 1;
    /* Check the second talbe only if the rehashing happened. */
    if (!first_rehashing_step) {
        if (check(t1,"table 1")) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int opt_verbose = 0;
    int opt_table0_size = 0;
    int opt_table1_size = 0;
    int opt_seed = 0;

    /* Parse arguments. */
    for (int j = 1; j < argc; j++) {
        int moreargs = argc-j-1;
        if (!strcasecmp(argv[j],"--verbose")) {
            opt_verbose = 1;
        } else if (!strcasecmp(argv[j],"--seed") && moreargs >= 1) {
            opt_seed = atoi(argv[++j]);
        } else if (!strcasecmp(argv[j],"--size") && moreargs >= 2) {
            opt_table0_size = atoi(argv[++j]);
            opt_table1_size = atoi(argv[++j]);
        } else if (!strcasecmp(argv[j],"--help")) {
            printf("Usage:\n");
            printf(" --help              Print this help.\n");
            printf(" --seed <seed>       Use this PRNG seed.\n");
            printf(" --size <t0> <t1>    Run with the specified sizes\n");
            exit(0);
        } else {
            printf("Syntax error\n");
            exit(1);
        }
    }

    if (opt_seed) {
        /* If a specific seed was given, use it. */
        srand(opt_seed);
    } else if (opt_table0_size || opt_table1_size) {
        /* Be predictable when user asks for a specific test. */
        srand(1234);
    } else {
        srand(time(NULL));
    }
    dictht t0, t1;

    while(1) {
        t0.size = 1<<(rand()%8);

        /* Pick a different size of t1. */
        do {
            t1.size = 1<<(rand()%8);
        } while(t1.size == t0.size);

        /* Override with user options if needed. */
        if (opt_table0_size) t0.size = opt_table0_size;
        if (opt_table1_size) t1.size = opt_table1_size;

        t0.sizemask = t0.size-1;
        t1.sizemask = t1.size-1;
        memset(t0.visited,0,sizeof(t0.visited));
        memset(t1.visited,0,sizeof(t1.visited));

        if (test_scan(&t0,&t1,opt_verbose)) exit(1);

        /* Stop after the first test if any of the tables size
         * was given by the user. */
        if (opt_table0_size || opt_table1_size) break;
    }

    return 0;
}

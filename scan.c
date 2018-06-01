#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    unsigned long size;
    unsigned long sizemask;
    int visited[256];
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
                       unsigned long v,
                       void *privdata)
{
    unsigned long m0, m1;

    if (t1 == NULL) {
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        printf("Single[%lu]\n", v&m0);
        t0->visited[v&m0] = 1;
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
        printf("Small[%lu]\n", v&m0);
        t0->visited[v&m0] = 1;

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        do {
            /* Emit entries at cursor */
            printf("Big[%lu]\n", v&m1);
            t1->visited[v&m1] = 1;

            /* Increment bits not covered by the smaller mask */
            v = (((v | m0) + 1) & ~m0) | (v & m0);

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

    /* Set unmasked bits so incrementing the reversed cursor
     * operates on the masked bits of the smaller table */
    v |= ~m0;

    /* Increment the reverse cursor */
    v = rev(v);
    v++;
    v = rev(v);

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

int main(void) {
    srand(time(NULL));
    dictht t0, t1;

    t0.size = 8;
    t0.sizemask = t0.size-1;
    memset(t0.visited,0,sizeof(t0.visited));

    t1.size = 32;
    t1.sizemask = t1.size-1;
    memset(t1.visited,0,sizeof(t1.visited));

    unsigned long cursor = 0;
    int iteration = 0;
    int before_rehashing = 1;
    int first_rehashing_step = 1;
    do {
        if (before_rehashing) {
            cursor = dictScan(&t0, NULL, cursor, NULL);
            if (!(rand() % t0.size)) {
                before_rehashing = 0;
                printf("Rehashing to new table: %lu -> %lu\n",
                    t0.size, t1.size);
            }
        } else {
            if (first_rehashing_step) {
                expand(&t0,&t1);
                first_rehashing_step = 0;
            }
            cursor = dictScan(&t0, &t1, cursor, NULL);
        }
        printf("cursor %lu\n", cursor);
        iteration++;
    } while (cursor != 0);

    /* Check that the two tables were visited. */
    if (check(&t0,"table 0")) exit(1);
    /* Check the second talbe only if the rehashing happened. */
    if (!first_rehashing_step) {
        if (check(&t1,"table 1")) exit(1);
    }

    return 0;
}

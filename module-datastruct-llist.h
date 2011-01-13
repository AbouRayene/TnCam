
/* singularly linked-list */

#ifndef DATASTRUCT_LLIST_H_
#define DATASTRUCT_LLIST_H_

#include <pthread.h>

typedef struct llnode LL_NODE;
struct llnode {
    void *obj;
    LL_NODE *nxt, *prv;
};

typedef struct llist LLIST;
struct llist {
    void *obj;
    LL_NODE *initial;
    pthread_mutex_t *lock;
    int count;
};

typedef struct lliter LL_ITER;
struct lliter {
    LLIST *l;
    LL_NODE *cur;
};

LLIST *ll_create();             // create llist, return ptr to llist
LLIST *ll_create_nolock();             // create llist, return ptr to llist (disable locking)
void ll_destroy(LLIST *l);      // same as ll_clear_abstract() but frees up LLIST mem as well
void ll_destroy_data(LLIST *l); // same as ll_clear_data() but frees up obj allocations as well
void ll_clear(LLIST *l);        // frees up all llnodes nodes but not data held in obj ptrs
void ll_clear_data(LLIST *l);   // same as ll_clear_data() but frees up obj allocations as well

void ll_append(LLIST *l, void *obj);                // append obj to llist
void ll_append_nolock(LLIST *l, void *obj);         // append obj to llist (no locking, use when iter)
void ll_insert_at(LLIST *l, void *obj, int pos)       ;    // insert at pos
void ll_insert_at_nolock(LLIST *l, void *obj, int pos);    // insert at pos (no locking, use when iter)

LL_ITER *ll_iter_create(LLIST *l);              // return ptr to iterator obj
void ll_iter_release(LL_ITER *it);              // free up the iterator obj
void *ll_iter_next(LL_ITER *it);                // iterate to and return next llnode obj, returns NULL at end
void *ll_iter_peek(LL_ITER *it, int offset);    // return obj at offset from iterator but do not iterate
void ll_iter_reset(LL_ITER *it);                // reset itrerator to first llnode
void ll_iter_insert(LL_ITER *it, void *obj);    // insert obj at iterator node
void *ll_iter_remove(LL_ITER *it);              // remove llnode at iterator, returns ptr to the llnode obj removed
void ll_iter_remove_data(LL_ITER *it);          // remove llnode and free llnode obj

int ll_count(LLIST *l);                 // return number of items in list
void *ll_has_elements(LLIST *l);

#endif


/*
 * Copyright 2018 Sam Ainsworth
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose,  provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 */

#include "private/gc_priv.h"

#include <pthread.h>

void GC_walk_freeing_list (void);
void GC_walk_freeing_link (void);
void GC_walk_freeing_link_hblker(void);
void potential_remap (hdr *, ptr_t p);
void potential_unmap(hdr * hhdr, ptr_t p);

int64_t freeing_list_size;
int64_t freeing_link_size;
int64_t failed_frees;
int64_t  unmapped;
int64_t  unmapped_since_gc;
u_int64_t big_sections;
int zq;

pthread_mutex_t unmap_lock;


#define UNMAP_COST(x) (x>=GC_page_size)

#define UNMAP_COST2(x) (x>=10*GC_page_size)

#ifndef FREEING_LIST
#define FREEING_LIST 1
#endif

#ifndef BLOCK_CULL
#define BLOCK_CULL 1
#endif

#ifndef UNMAP_PAGES
#define UNMAP_PAGES 1
#endif

#ifndef FREE_REFRESH
#define FREE_REFRESH 1
#endif

#ifndef ALWAYS_WALK
#define ALWAYS_WALK 0
#endif

#define BIGSECTION (big_sections > 8ll*((u_int64_t)GC_heapsize-unmapped))

#define WALK_GC_FREEING_LIST (!FREEING_LIST || ALWAYS_WALK || BIGSECTION ||  (((freeing_list_size+freeing_link_size -failed_frees)*4ll) > (((u_int64_t)GC_heapsize-unmapped))))

#define IS_MY_MAPPED(hhdr) (((hhdr) -> hb_flags & WAS_UNMAPPED) == 0) 


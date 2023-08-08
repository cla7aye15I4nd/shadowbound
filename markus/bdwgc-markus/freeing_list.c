
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
#include "private/gc_locks.h"

#include <pthread.h>



#include <stdio.h>
#include <string.h>

#include <sys/mman.h>

#include "private/freeing_list.h"

STATIC struct hblkhdr * flist_head = NULL;
STATIC struct hblkhdr * flist_tail = NULL;

STATIC  struct hblkhdr* flist_head_inner = NULL;
STATIC  struct hblkhdr* flist_tail_inner = NULL;

#define XOR (1ul<<52)

static void* flink_head = (void*)XOR;
static void* flink_tail = (void*)XOR;

//static void* flink_head_inner = NULL;
//static void* flink_tail_inner = NULL;

static pthread_mutex_t walking_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t freeing_list_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t walking_link_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t freeing_link_lock=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t unmap_lock=PTHREAD_MUTEX_INITIALIZER;

#if 0
#define TRYLOCK DCL_LOCK_STATE;int need_lock = I_DONT_HOLD_LOCK();if(need_lock)LOCK();

#define TRYUNLOCK if(need_lock)UNLOCK();
#endif

#define IS_MAPPED(hhdr) (((hhdr) -> hb_flags & WAS_UNMAPPED) == 0)

//TODO: make thread safe?

int64_t freeing_list_size = 0;
int64_t freeing_link_size = 0;
//int64_t allocated_data = 0;
int64_t failed_frees = 0;
int zq=0;


u_int64_t big_sections = 0;





void write_flink_head(void* p) {
    flink_head = (void*)((word)p ^XOR);
}

void* read_flink_head(void) {
    return (void*)((word)flink_head ^XOR);
}


void write_flink_tail(void* p) {
    flink_tail = (void*)((word)p ^XOR);
}

void* read_flink_tail(void) {
    return (void*)((word)flink_tail ^XOR);
}


void write_objlink(void* p, void* q) {
    obj_link(p) = (void*)((word)q ^XOR);
}

void* read_objlink(void* p) {
    return (void*)((word)obj_link(p) ^XOR);
}




void GC_add_to_freeing_link (void* p, size_t sz) {
    pthread_mutex_lock(&freeing_link_lock);
    if(read_flink_head() == NULL) {
        //flink_head = p;
        write_flink_head(p);
        write_flink_tail(p);
        //flink_tail = p;
    } else {
        write_objlink(read_flink_tail(),p);
        //obj_link(read_flink_tail()) = p;
        write_flink_tail(p);
        //flink_tail = p;
    }
    freeing_link_size += sz;
    pthread_mutex_unlock(&freeing_link_lock);
}

void GC_add_to_freeing_list (struct hblkhdr * entry) {

    pthread_mutex_lock(&freeing_list_lock);
    if(entry->hb_next || entry->hb_prev || entry == flist_head) {
        //printf("double free %p -> %p  <- %p | %p\n",entry, entry->hb_next, entry->hb_prev, flist_head);
        //pthread_mutex_unlock(&mtx);
        pthread_mutex_unlock(&freeing_list_lock);
        return; //double free...
    }
    if(!UNMAP_COST(entry->hb_sz) || !UNMAP_PAGES)
    	freeing_list_size += HBLKSIZE * OBJ_SZ_TO_BLOCKS(entry->hb_sz);
    if(flist_tail==NULL) {
        flist_head = entry;
        flist_tail = entry;

        flist_head->hb_next = NULL; //TODO: may not need
        flist_head->hb_prev = NULL;
    } else {
        flist_tail->hb_next = entry->hb_block;

        entry->hb_prev = flist_tail->hb_block;
        flist_tail = entry;
        flist_tail->hb_next = NULL; //TODO: may not need
    }
    pthread_mutex_unlock(&freeing_list_lock);
}
ptr_t GC_unmap_start(ptr_t start, word bytes);
ptr_t GC_unmap_end(ptr_t start, word bytes);


ptr_t GC_unmap_start(ptr_t start, word bytes)
{
    ptr_t result = start;
    //Round start to next page boundary.
    result += GC_page_size - 1;
    result = (ptr_t)((word)result & ~(GC_page_size - 1));
    if (result + GC_page_size > start + bytes) return 0;
    return result;
}




// Compute end address for an unmap operation on the indicated
// block.
ptr_t GC_unmap_end(ptr_t start, word bytes)
{
    ptr_t end_addr = start + bytes;
    end_addr = (ptr_t)((word)end_addr & ~(GC_page_size - 1));
    return end_addr;
}


void GC_CALL GC_safe_free(void * p) {

    //printf("Starting free of %p\n", p);
    struct hblk *h;
    struct hblkhdr *hhdr;
    int knd;


    if (p == 0) return; //Required by ANSI

    h = HBLKPTR(p);
    hhdr = HDR(h);
    if(!hhdr) hhdr = GC_find_header((ptr_t)GC_base(p));
    if(!hhdr) {
       // printf("no header free\n");
        return; //TODO
#if 0

        void* handle = dlopen("libc.so.6",RTLD_LAZY);
        // NOTE: libc.so.6 may *not* exist on Alpha and IA-64 architectures.
        if(!handle) {
            assert(0);
        }

        void (*libc_free)(void*) = dlsym(handle, "free");
        //printf("libc free\n");
        libc_free(p);
        return;

#endif
    }


    knd = hhdr -> hb_obj_kind;
    size_t sz = (size_t)hhdr->hb_sz;
    size_t  ngranules = BYTES_TO_GRANULES(sz);

    if(IS_UNCOLLECTABLE(knd)) {

        //printf("actual free\n");
        LOCK();
        GC_actual_free(p);
        UNLOCK();
        return;
    }



    if (EXPECT(ngranules <= MAXOBJGRANULES, TRUE)) {
    	//struct obj_kind *  ok = &GC_obj_kinds[knd];
      // if (ok -> ok_init && EXPECT(sz > sizeof(word), TRUE)) {
       //     BZERO((word *)p + 1, sz-sizeof(word));
      //  }
        GC_add_to_freeing_link(p,sz);

    } else {


        if(hhdr->hb_next || hhdr == flist_head || hhdr->hb_prev) {
            printf("double free %p -> %p  <- %p | %p\n",hhdr, hhdr->hb_next, hhdr->hb_prev, flist_head);
            return;
        }


        GC_add_to_freeing_list(hhdr);

        //LOCK();
        //GC_actual_free(p);
        //UNLOCK();

#if UNMAP_PAGES
if(UNMAP_COST(hhdr->hb_sz)) {
        if(!GC_collecting) {
        LOCK();
        potential_unmap(hhdr,h);
        UNLOCK();
        } else madvise((ptr_t)h,(size_t)hhdr->hb_sz,MADV_FREE);

           // pthread_mutex_lock(&unmap_lock);
            //unmapped_since_gc += HBLKSIZE * OBJ_SZ_TO_BLOCKS(sz);
           // pthread_mutex_unlock(&unmap_lock);
}
#endif
    }





}

//These shouldn't be enabled at the same time as USE_MUNMAP. The definitions of GC_unmap* should see to that.

void potential_unmap(hdr * hhdr, ptr_t p) {
    if(1) {
    size_t bytes = HBLK_IS_FREE(hhdr)? hhdr->hb_sz : HBLKSIZE * OBJ_SZ_TO_BLOCKS(hhdr->hb_sz);
        //TODO: check for remapping.
        //TODO: change size of freeing list?
        //GC_unmap((ptr_t)hhdr, (size_t)hhdr->hb_sz);
        ptr_t start = (ptr_t)p;
        //This is what its size will be on the freeing list.
        // - ((ptr_t)p-(ptr_t)hhdr);//(size_t)hhdr->hb_sz - ((ptr_t)p-(ptr_t)hhdr);
        ptr_t start_addr = start;//GC_unmap_start(start, bytes);
        ptr_t end_addr = start + bytes;//GC_unmap_end(start, bytes);
        big_sections+=bytes;


        word len = end_addr - start_addr;
	        assert(((word)start_addr % GC_page_size) == 0);
	                assert(((word)len % GC_page_size) == 0);
	                assert(IS_MY_MAPPED(hhdr));
	                assert(len != 0);

       // if(start_addr !=0 && len !=0) {
            //printf("Cleared %p to %p, size %ld\n", start_addr, end_addr, bytes);
#if 1
            int res = munmap((ptr_t)start_addr,(size_t)len );
#else
	 int res = madvise((ptr_t)start_addr,(size_t)len ,MADV_FREE);
	 mprotect((ptr_t)start_addr,(size_t)len ,PROT_NONE);
#endif
		//if(res)printf("Failed free %d\n", res);
		if(res) {
		//assert(res !=-1);
		    //fprintf(stderr, "%s\n", explain_munmap((ptr_t)start_addr,(size_t)len)); 
			//exit(EXIT_FAILURE);
			return;
		}
           // assert(!res);   
            

            hhdr -> hb_flags |= WAS_UNMAPPED;
            //       clock_t cstart = clock(), diff;
                        pthread_mutex_lock(&unmap_lock);
            unmapped += len;
            
            //unmapped_since_gc += len; //moved because we don't want this for small hblks
            pthread_mutex_unlock(&unmap_lock);  

       // }
    }
}


void potential_remap(hdr * hhdr, ptr_t hb) {



    if(!IS_MY_MAPPED(hhdr)) {
    

    
        //remap of freed data
        
        word bytes = HBLKSIZE * OBJ_SZ_TO_BLOCKS(hhdr->hb_sz);
        ptr_t start_addr = hb;// GC_unmap_start(start, bytes);
        ptr_t end_addr = hb + bytes;//GC_unmap_end(start, bytes);
        assert(start_addr != NULL);

        word len = end_addr - start_addr;
        
	assert(((word)start_addr % GC_page_size) == 0);
	                assert(((word)len % GC_page_size) == 0);
	                assert(len != 0);

        //printf("Remapped %p to %p, size %ld\n", start_addr, end_addr, len);
        //clock_t cstart = clock(), diff;


        
#if 1
        ptr_t new = mmap((ptr_t)start_addr,(size_t)len,PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
                assert(new == start_addr);

      /*     if(new != start_addr) {
           	printf("Failed length %ld %p vs %p\n",len, new, start_addr);
                munmap((ptr_t)new,(size_t)len);
           	for(ptr_t x = start_addr; x < end_addr; x+=GC_page_size) {
           	 mmap((ptr_t)x,(size_t)GC_page_size,PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|       MAP_FIXED, -1, 0);
           	printf("Failed Offset %ld\n",(size_t)(x-start_addr));
           	}
           }
*/
#else
	 mprotect((ptr_t)start_addr,(size_t)len ,PROT_READ|PROT_WRITE);
	 madvise((ptr_t)start_addr,(size_t)len ,MADV_NORMAL);

#endif

        // diff = clock() - cstart;
        //  int msec = diff * 1000 / CLOCKS_PER_SEC;
        // printf("Remap Time taken :%d: milliseconds\n", msec);
        //printf("Remap\n");

                pthread_mutex_lock(&unmap_lock);
        unmapped -= len;
        pthread_mutex_unlock(&unmap_lock);

        hhdr->hb_flags &= ~WAS_UNMAPPED;
    }

}


void GC_freeing_detach (struct hblkhdr* hb) {

    assert((!flist_tail_inner && !flist_head_inner) || flist_tail_inner->hb_next == NULL);


    if(hb->hb_prev == NULL && hb->hb_next == NULL) {
        flist_head_inner = NULL;
        flist_tail_inner = NULL;
        return;
    }

    if(hb->hb_prev == NULL) {
        struct hblkhdr* next = HDR(hb->hb_next);
        flist_head_inner = next;
        next->hb_prev = NULL;

    }
    else {
        HDR((hb->hb_prev))->hb_next = hb->hb_next; //This HDR call may not be ideal for performance?
    }

    if(hb->hb_next == NULL) {
        struct hblkhdr* prev = HDR(hb->hb_prev);
        flist_tail_inner = prev;
        prev->hb_next = NULL;
    }
    else HDR((hb->hb_next))->hb_prev = hb->hb_prev;

    hb->hb_prev = NULL;
    hb->hb_next = NULL;

}

GC_API void GC_CALL GC_actual_free(void * p)
{
    // printf("Actual free of %p\n",p);
    struct hblk *h;
    hdr *hhdr;
    size_t sz; /* In bytes */
    size_t ngranules;   /* sz in granules */
    int knd;
    struct obj_kind * ok;
    DCL_LOCK_STATE;

    if (p == 0) return;
    /* Required by ANSI.  It's not my fault ...     */
#   ifdef LOG_ALLOCS
    GC_log_printf("GC_free(%p) after GC #%lu\n",
                  p, (unsigned long)GC_gc_no);
#   endif
    h = HBLKPTR(p);
    hhdr = HDR(h);
#   if defined(REDIRECT_MALLOC) && \
        ((defined(NEED_CALLINFO) && defined(GC_HAVE_BUILTIN_BACKTRACE)) \
         || defined(GC_SOLARIS_THREADS) || defined(GC_LINUX_THREADS) \
         || defined(MSWIN32))
    /* This might be called indirectly by GC_print_callers to free  */
    /* the result of backtrace_symbols.                             */
    /* For Solaris, we have to redirect malloc calls during         */
    /* initialization.  For the others, this seems to happen        */
    /* implicitly.                                                  */
    /* Don't try to deallocate that memory.                         */
    if (0 == hhdr) return;
#   endif
    GC_ASSERT(GC_base(p) == p);
    sz = (size_t)hhdr->hb_sz;
    ngranules = BYTES_TO_GRANULES(sz);
    knd = hhdr -> hb_obj_kind;
    //potential_remap(hhdr);
    //int need_lock = I_DONT_HOLD_LOCK();

    ok = &GC_obj_kinds[knd];
    if (EXPECT(ngranules <= MAXOBJGRANULES, TRUE)) {
        void **flh;


        //LOCK();


        GC_bytes_freed += sz;
        if (IS_UNCOLLECTABLE(knd)) GC_non_gc_bytes -= sz;
        /* Its unnecessary to clear the mark bit.  If the       */
        /* object is reallocated, it doesn't matter.  O.w. the  */
        /* collector will do it, since it's on a free list.     */
        if (ok -> ok_init && EXPECT(sz > sizeof(word), TRUE)) {
            BZERO((word *)p + 1, sz-sizeof(word));
        }
        flh = &(ok -> ok_freelist[ngranules]);
        obj_link(p) = *flh;
        *flh = (ptr_t)p;


    } else {
        size_t nblocks = OBJ_SZ_TO_BLOCKS(sz);

        GC_bytes_freed += sz;
        if (IS_UNCOLLECTABLE(knd)) GC_non_gc_bytes -= sz;
        if (nblocks > 1) {
            GC_large_allocd_bytes -= nblocks * HBLKSIZE;
        }
        GC_freehblk(h);
    }
    //UNLOCK();
}


void GC_walk_freeing_list (void) {
    pthread_mutex_lock(&walking_lock);
    big_sections=0;
    // printf("Walking freeing list\n");
    //pthread_mutex_lock(&mtx);
    struct hblkhdr* current;
    struct hblkhdr* future;
    failed_frees =0;
    int round = 0;
    pthread_mutex_lock(&freeing_list_lock);
    flist_head_inner = flist_head;
    flist_tail_inner = flist_tail;
    flist_head = NULL;
    flist_tail = NULL;
    word freed = 0;
    pthread_mutex_unlock(&freeing_list_lock);
    for(current = flist_head_inner; current!=NULL; current = future) {
        word bit_no = 0;
        word *p, *plim;
        struct hblk* hbp = current->hb_block;
        future = current->hb_next == NULL ? NULL : HDR(current->hb_next);
        word sz = current->hb_sz;
        //printf("Trying to collect %p\n",hbp);
        round++;

        int found = 0;


            if (mark_bit_from_hdr(current, 0)) {
                found = 1;
                if(IS_MY_MAPPED(current)) {
                if(UNMAP_COST(sz))
                	potential_unmap(current,current->hb_block);
                else failed_frees += sz;

                }
                //printf("marked %p at %ld\n",current,bit_no);
            }
            
        if(found) {
            //printf("Failed to collect %p round %d\n", current, round);

        } else {
            GC_freeing_detach(current);
            //printf("Collected size %ld\n", sz);
            if(IS_MY_MAPPED(current)) {
            	freed += HBLKSIZE * OBJ_SZ_TO_BLOCKS(sz);
            }
#if 0
            if(!IS_MAPPED(current)) {
                ptr_t start = (ptr_t)p;
                word bytes = HBLKSIZE * OBJ_SZ_TO_BLOCKS(sz);//(size_t)hhdr->hb_sz - ((ptr_t)p-(ptr_t)hhdr);
                ptr_t start_addr = GC_unmap_start(start, bytes);
                ptr_t end_addr = GC_unmap_end(start, bytes);
                word len = end_addr - start_addr;

                pthread_mutex_lock(&unmap_lock);
                unmapped_since_gc -= len;
                pthread_mutex_unlock(&unmap_lock);
                // freeing_list_size += len; //to counteract earlier decrease.

            }
#endif
            //potential_remap(current);
            GC_actual_free(current->hb_block);
        }
    }

    pthread_mutex_lock(&freeing_list_lock);
    // allocated_data -= freed;
    //  total_freed += freed;
    // printf("freed %ld from list, size was %ld, heapsize %ld\n", freed, freeing_list_size,  GC_heapsize - unmapped);
    freeing_list_size -= freed;
    //if(allocated_data < 0) allocated_data = freeing_list_size;

    if(flist_head == NULL) {
        flist_head = flist_head_inner;
    }
    else {
        flist_tail->hb_next = flist_head_inner ? flist_head_inner->hb_block: NULL;

    }
    if(flist_tail_inner != NULL) flist_tail = flist_tail_inner;
    pthread_mutex_unlock(&freeing_list_lock);
    //pthread_mutex_unlock(&mtx);

    flist_tail_inner = NULL;
    flist_head_inner = NULL;
    pthread_mutex_lock(&unmap_lock);
    unmapped_since_gc = 0;
    pthread_mutex_unlock(&unmap_lock);

    pthread_mutex_unlock(&walking_lock);

    //TRYUNLOCK;
    //printf("Exit\n");
}



void GC_walk_freeing_link (void) {
    //  printf("Walking freeing link\n");
    //pthread_mutex_lock(&mtx);
    //TRYLOCK;
    void* current;
    void* future;
    void* prev = NULL;


    pthread_mutex_lock(&walking_link_lock);
    pthread_mutex_lock(&freeing_link_lock);
    void* flink_head_inner = read_flink_head();
    void* flink_tail_inner = read_flink_tail();
    flink_head = (void*)XOR;
    flink_tail = (void*)XOR;
    pthread_mutex_unlock(&freeing_link_lock);

    word freed = 0;

    if(flink_head_inner == NULL) {
        pthread_mutex_unlock(&walking_link_lock);
        return;
    }
    // printf("Walking freeing link2\n");
    for(current = flink_head_inner; ; current = future) {


        struct hblk* hbp = HBLKPTR(current);
        hdr * hhdr = HDR(hbp);
        if(!hhdr) hhdr = GC_find_header((ptr_t)GC_base(current));
        assert(hhdr!=NULL);
        future = current == flink_tail_inner? NULL : read_objlink(current);
        word sz = hhdr->hb_sz;

        if(GC_is_marked(current)) {
            //printf("Failed to Collect size %ld\n", sz);
            failed_frees += sz; //TODO: not correct?
            prev = current;
            if(current == flink_tail_inner) break;
            else continue;
        }
        //detach
        if(prev) {
            write_objlink(prev,read_objlink(current));
        } else {
            flink_head_inner = (current == flink_tail_inner)? NULL : read_objlink(current);
        }
        obj_link(current) = NULL;


        //printf("Kollected size %ld\n", sz);
        freed+= sz;



        GC_actual_free(current);
        if(current == flink_tail_inner) {
            flink_tail_inner = prev;
            break;
        }

    }

    pthread_mutex_lock(&freeing_link_lock);

    // printf("freed %ld from link, size was %ld, allocated %ld, heapsize %ld\n", freed, freeing_link_size, allocated_data, GC_heapsize - unmapped);

    freeing_link_size -= freed;

    //if(allocated_data < 0) allocated_data = freeing_link_size;


    if(read_flink_head() == NULL) {
        write_flink_head(flink_head_inner);
    } else {
        write_objlink(read_flink_tail(),flink_head_inner);
    }
    if(flink_tail_inner != NULL) write_flink_tail(flink_tail_inner);
    pthread_mutex_unlock(&freeing_link_lock);

    flink_tail_inner = NULL;
    flink_head_inner = NULL;

    pthread_mutex_unlock(&walking_link_lock);
    //printf("Exit\n");
}





void GC_mark_freeing_link (void) {
    //  printf("Walking freeing link\n");
    //pthread_mutex_lock(&mtx);
    //TRYLOCK;
    void* current;
    void* future;
    void* prev = NULL;


    pthread_mutex_lock(&walking_link_lock);
    pthread_mutex_lock(&freeing_link_lock);
    void* flink_head_inner = read_flink_head();
    void* flink_tail_inner = read_flink_tail();
    pthread_mutex_unlock(&freeing_link_lock);

    if(flink_head_inner == NULL) {
        pthread_mutex_unlock(&walking_link_lock);
        return;
    }
    // printf("marking freeing link\n");
    for(current = flink_head_inner; ; current = future) {

        word bit_no = 0;
        struct hblk* hbp = HBLKPTR(current);
        hdr * hhdr = HDR(hbp);
        if(!hhdr) hhdr = GC_find_header((ptr_t)GC_base(current));
        assert(hhdr!=NULL);
        future = current == flink_tail_inner? NULL : read_objlink(current);

        hhdr->hb_n_marks = HBLK_OBJS(hhdr -> hb_sz);

        if(current == flink_tail_inner) {
            break;
        }

    }


    pthread_mutex_unlock(&walking_link_lock);
}


void GC_clear_freeing_link (void) {
    //  printf("Walking freeing link\n");
    //pthread_mutex_lock(&mtx);
    //TRYLOCK;
    void* current;
    void* future;
    void* prev = NULL;


    pthread_mutex_lock(&walking_link_lock);
    pthread_mutex_lock(&freeing_link_lock);
    void* flink_head_inner = read_flink_head();
    void* flink_tail_inner = read_flink_tail();
    pthread_mutex_unlock(&freeing_link_lock);

    if(flink_head_inner == NULL) {
        pthread_mutex_unlock(&walking_link_lock);
        return;
    }
    // printf("clearing freeing link\n");
    for(current = flink_head_inner; ; current = future) {


        word bit_no = 0;
        struct hblk* hbp = HBLKPTR(current);
        hdr * hhdr = HDR(hbp);
        if(!hhdr) hhdr = GC_find_header((ptr_t)GC_base(current));
        assert(hhdr!=NULL);
        future = current == flink_tail_inner? NULL : read_objlink(current);

        if (hhdr->hb_n_marks == 0) {
            ABORT_ARG1("Overly unmarked",
                       " of %p", (void *)hhdr);
        }


        if(!GC_is_marked(current)) {
            hhdr->hb_n_marks --; //TODO: not double-free safe? Or does the cyclic link prevent that?
        }




        if(current == flink_tail_inner) {
            break;
        }

    }

    pthread_mutex_unlock(&walking_link_lock);
}



GC_INNER void GC_freelinkhblk(struct hblk *hbp)
{
    struct hblk *next, *prev;
    hdr *hhdr, *prevhdr, *nexthdr;
    word size;

    GET_HDR(hbp, hhdr);
    size = HBLKSIZE * OBJ_SZ_TO_BLOCKS(hhdr->hb_sz);
    GC_remove_counts(hbp, size);
    hhdr->hb_sz = size;
    if (HBLK_IS_FREE(hhdr)) {
        ABORT_ARG1("Duplicate large block deallocation",
                   " of %p", (void *)hbp);
    }
    hhdr -> hb_flags |= FREE_BLK;
    //TODO: remove header after walked entire freelink...

    next = (struct hblk *)((ptr_t)hbp + size);
    GET_HDR(next, nexthdr);
    prev = GC_free_block_ending_at(hbp);
    // Coalesce with successor, if possible
    if(0 != nexthdr && HBLK_IS_FREE(nexthdr) && IS_MY_MAPPED(nexthdr)
            && (signed_word)(hhdr -> hb_sz + nexthdr -> hb_sz)  > 0
      ) {
        GC_remove_from_fl(nexthdr);
        hhdr -> hb_sz += nexthdr -> hb_sz;
        GC_remove_header(next);
    }
    // Coalesce with predecessor, if possible.
    if (0 != prev) {
        prevhdr = HDR(prev);
        if (IS_MY_MAPPED(prevhdr)
                && (signed_word)(hhdr -> hb_sz + prevhdr -> hb_sz) > 0) {
            GC_remove_from_fl(prevhdr);
            prevhdr -> hb_sz += hhdr -> hb_sz;

            GC_remove_header(hbp);
            hbp = prev;
            hhdr = prevhdr;
        }
    }

    GC_large_free_bytes += size;
    GC_add_to_fl(hbp, hhdr);
}

void GC_free_freeing_link (void) {
    //  printf("Walking freeing link\n");
    //pthread_mutex_lock(&mtx);
    //TRYLOCK;
    void* current;
    void* future;
    void* prev = NULL;


    pthread_mutex_lock(&walking_link_lock);
    pthread_mutex_lock(&freeing_link_lock);
    void* flink_head_inner = read_flink_head();
    void* flink_tail_inner = read_flink_tail();
    flink_head = (void*)XOR;
    flink_tail = (void*)XOR;
    pthread_mutex_unlock(&freeing_link_lock);

    word freed = 0;

    if(flink_head_inner == NULL) {
        pthread_mutex_unlock(&walking_link_lock);
        return;
    }
    //printf("freeing freeing link\n");;
    for(current = flink_head_inner; ; current = future) {


        struct hblk* hbp = HBLKPTR(current);
        hdr * hhdr = HDR(hbp);
        if(!hhdr) hhdr = GC_find_header((ptr_t)GC_base(current));
        assert(hhdr!=NULL);
        future = current == flink_tail_inner? NULL : read_objlink(current);
        word sz = hhdr->hb_sz;

        if(GC_is_marked(current)) {
            //printf("Failed to Collect size %ld\n", sz);
            failed_frees += sz; //TODO: not correct?
            prev = current;
            if(current == flink_tail_inner) break;
            else continue;
        }
        //detach
        if(prev) {
            write_objlink(prev,read_objlink(current));
        } else {
            flink_head_inner = (current == flink_tail_inner)? NULL : read_objlink(current);
        }
        obj_link(current) = NULL;


        //printf("Kollected size %ld\n", sz);
        //freed+= sz;

        if(HBLK_IS_FREEING(hhdr)) {


            hhdr->hb_n_marks++;

            if(hhdr->hb_n_marks == HBLK_OBJS(hhdr -> hb_sz)) {
                GC_large_allocd_bytes -= HBLKSIZE;
                hhdr -> hb_flags &= ~FREEING_BLK;
                hhdr->hb_n_marks = 0;
                GC_freehblk(hhdr->hb_block);
                freed += HBLKSIZE;
            }

        }
        else if(hhdr->hb_n_marks ==0 ) {
            hhdr->hb_n_marks++;
            hhdr -> hb_flags |= FREEING_BLK;
        } else {
            //  printf("actual free %p\n",hbp);
            GC_actual_free(current);
            freed+= sz;
        }
        if(current == flink_tail_inner) {
            flink_tail_inner = prev;
            break;
        }

    }

    pthread_mutex_lock(&freeing_link_lock);

    // printf("freed %ld from link, size was %ld,  heapsize %ld\n", freed, freeing_link_size,  GC_heapsize - unmapped);

    freeing_link_size -= freed;

    //if(allocated_data < 0) allocated_data = freeing_link_size;


    if(read_flink_head() == NULL) {
        write_flink_head(flink_head_inner);
    } else {
        write_objlink(read_flink_tail(),flink_head_inner);
    }
    if(flink_tail_inner != NULL) write_flink_tail(flink_tail_inner);
    pthread_mutex_unlock(&freeing_link_lock);



    pthread_mutex_unlock(&walking_link_lock);
    //printf("Exit\n");
}





void GC_walk_freeing_link_hblker (void) {
    //  printf("Walking freeing link\n");
    //pthread_mutex_lock(&mtx);
    //TRYLOCK;
    ptr_t current;
    ptr_t future;
    ptr_t prev = NULL;


    pthread_mutex_lock(&walking_link_lock);
    pthread_mutex_lock(&freeing_link_lock);
    void* flink_head_inner = read_flink_head();
    void* flink_tail_inner = read_flink_tail();
    flink_head = (void*)XOR;
    flink_tail = (void*)XOR;
    pthread_mutex_unlock(&freeing_link_lock);
    


    int64_t freed = 0;
    word freehblked = 0;
    word freehblkedtried = 0;
    word firstfreed = 0;
    word secondfreed = 0;

    
    word seen = 0;

    if(flink_head_inner == NULL) {
        pthread_mutex_unlock(&walking_link_lock);
        return;
    }

    //Free any elements where number of marks in block > 0, and individual element free. If number of marks 0, set to max size.


    for(current = flink_head_inner; ; current = future) {


        struct hblk* hbp = HBLKPTR(current);
        hdr * hhdr = HDR(hbp);
        if(!hhdr) hhdr = GC_find_header((ptr_t)GC_base(current));
        assert(hhdr!=NULL);
        future = current == flink_tail_inner? NULL : read_objlink(current);
        word sz = hhdr->hb_sz;
        seen +=sz;

        if(GC_is_marked(current)) {
            //printf("Failed to Collect size %ld\n", sz);
            failed_frees += sz;
            prev = current;
        } else if(HBLK_IS_FREEING(hhdr)) {
            assert(hhdr->hb_n_marks > 0);
            hhdr -> hb_n_marks--;
            //printf("%p freeing marks %ld\n", hbp, hhdr -> hb_n_marks);
            
           // if(hhdr->hb_n_marks ==0) printf("Will free hblk %p\n", hbp);
            prev = current;

        } else if(hhdr->hb_n_marks > 0) {

            //detach, free and remove
            if(prev) {
                write_objlink(prev,read_objlink(current));
            } else {
                flink_head_inner = (current == flink_tail_inner)? NULL : read_objlink(current);
            }
            obj_link(current) = NULL;

            firstfreed += sz;
            freed +=sz;
            GC_actual_free(current);

        } else { //num_marks ==0

            hhdr -> hb_flags |= FREEING_BLK;
            hhdr -> hb_n_marks = HBLK_OBJS(hhdr -> hb_sz)-1;
            //printf("%p freeing marks %ld\n", hbp, hhdr -> hb_n_marks);
            prev = current;
            freehblkedtried+= HBLKSIZE;

        }

        if(current == flink_tail_inner) {
            flink_tail_inner = prev;
            break;
        }

    }

#ifdef FREE_REFRESH
    //Now, walk free list and add any elements in blocks with FREEING_BLK attached, decrementing nmarks along the way and making sure mark unset.

    word size;        //current object size  
    unsigned kind;
    ptr_t q;


    for (kind = 0; kind < GC_n_kinds && flink_head_inner != NULL && freehblkedtried !=0; kind++) {
        for (size = 1; size <= MAXOBJGRANULES; size++) {
            q = (ptr_t)GC_obj_kinds[kind].ok_freelist[size];
            prev = NULL;
            if (q != NULL) {
                struct hblk *h = HBLKPTR(q);
                struct hblk *last_h = h;
                hdr *hhdr = HDR(h);
                word sz = hhdr->hb_sz;

                for (;;) {

                    ptr_t next = (ptr_t)obj_link(q);

                    if(HBLK_IS_FREEING(hhdr)) {
                        word bit_no = MARK_BIT_NO((ptr_t)q - (ptr_t)h, sz);

                        if (mark_bit_from_hdr(hhdr, bit_no)) {
                            clear_mark_bit_from_hdr(hhdr, bit_no);
                        }
                        
                                    assert(hhdr->hb_n_marks > 0);

                        (hhdr -> hb_n_marks)--;
                        
                        //printf("%p marks %ld\n", h, hhdr -> hb_n_marks);
                        
                       // if(hhdr->hb_n_marks ==0) printf("Will free hblk %p due to free list\n", h);

                        //move q to freeing list, and out of free list.
                        GC_bytes_freed -= sz;
                        if (IS_UNCOLLECTABLE(kind)) GC_non_gc_bytes += sz;
                        freed -= sz; //for accounting later.
                        if(prev != NULL) obj_link(prev) = next; 
                        else GC_obj_kinds[kind].ok_freelist[size] = next;
                        obj_link(q) = NULL;
                        write_objlink(flink_tail_inner,q);
                        flink_tail_inner = q;
                    }
                    else {
                        prev = q;
                    }

                    q = next;
                    if (q == NULL)
                        break;

                    h = HBLKPTR(q);
                    if (h != last_h) {
                        last_h = h;
                        hhdr = HDR(h);
                        sz = hhdr->hb_sz;
                    }
                }
            }
        }

    }

#endif

    //Now, if marked, continue (old blocks left in). Otherwise delete from link, and (if FREEING_LIST set and nblks>0, add to free list, and increment marks, and if marks at max unset FREEING_LIST. If FREEING_LIST set and nblks==0, unset freeing_list, increment nblks. If FREEING_LIST not set, nblks++. If nblks = max_size, free block).

    prev = NULL;

    for(current = flink_head_inner; flink_head_inner != NULL && freehblkedtried !=0 /*to prevent loop from ever running*/; current = future) {


        word bit_no = 0;
        struct hblk* hbp = HBLKPTR(current);
        hdr * hhdr = HDR(hbp);
        if(!hhdr) hhdr = GC_find_header((ptr_t)GC_base(current));
        assert(hhdr!=NULL);
        future = current == flink_tail_inner? NULL : read_objlink(current);
        word sz = hhdr->hb_sz;

        if(GC_is_marked(current)) { //ignore marked.
            prev = current;
        } else {

            //detach, free and remove
            if(prev) {
                write_objlink(prev,read_objlink(current));
            } else {
                flink_head_inner = (current == flink_tail_inner)? NULL : read_objlink(current);
            }
            obj_link(current) = NULL;

            if(HBLK_IS_FREEING(hhdr) && hhdr->hb_n_marks > 0) {
                GC_actual_free(current); //dealt with "freeing something originally from free list", and accounting thereof.
                assert(hhdr->hb_n_marks < HBLK_OBJS(hhdr -> hb_sz));
                hhdr->hb_n_marks++;
                freed+=sz;
                secondfreed +=sz;

                if(hhdr->hb_n_marks == HBLK_OBJS(hhdr -> hb_sz)) {
                    hhdr -> hb_flags &= ~FREEING_BLK;
                    hhdr->hb_n_marks = 0;
                }

            } else if (HBLK_IS_FREEING(hhdr)) {
               // printf("Starting to free hblk %p\n", hbp);
                hhdr -> hb_flags &= ~FREEING_BLK;
                //hhdr->hb_n_marks = 0;
                hhdr->hb_n_marks++;
                freed+=sz;
            } else {
                freed+=sz;
                hhdr->hb_n_marks++;
                if(hhdr->hb_n_marks == HBLK_OBJS(hhdr -> hb_sz)) {
                     //       	printf("freed hblk %p\n", hbp);
                    //GC_large_allocd_bytes -= HBLKSIZE; //This breaks it.
                    hhdr->hb_n_marks = 0;
                    GC_freehblk(hhdr->hb_block);
                    freehblked += HBLKSIZE;
                    //freed += HBLKSIZE;
                }

            }


        }
        if(current == flink_tail_inner) {
            flink_tail_inner = prev;
            break;
        }

    }

    //clean up.

    pthread_mutex_lock(&freeing_link_lock);
    
    //printf("freeing link ran");

   // printf("freed %ld from link, %ld directly, size was %ld, seen %ld, indirect freed %ld, hblk freed %ld, heapsize %ld\n", freed, firstfreed, freeing_link_size, seen, secondfreed, freehblked, GC_heapsize - unmapped);

    freeing_link_size -= freed;

    if(read_flink_head() == NULL) {
        write_flink_head(flink_head_inner);
    } else {
        write_objlink(read_flink_tail(),flink_head_inner);
    }
    if(flink_tail_inner != NULL) write_flink_tail(flink_tail_inner);
    pthread_mutex_unlock(&freeing_link_lock);

    flink_tail_inner = NULL;
    flink_head_inner = NULL;

    pthread_mutex_unlock(&walking_link_lock);

}


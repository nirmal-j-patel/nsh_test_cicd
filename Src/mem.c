/*
 * mem.c - memory management
 *
 * This file is part of zsh, the Z shell.
 *
 * Copyright (c) 1992-1997 Paul Falstad
 * All rights reserved.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and to distribute modified versions of this software for any
 * purpose, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * In no event shall Paul Falstad or the Zsh Development Group be liable
 * to any party for direct, indirect, special, incidental, or consequential
 * damages arising out of the use of this software and its documentation,
 * even if Paul Falstad and the Zsh Development Group have been advised of
 * the possibility of such damage.
 *
 * Paul Falstad and the Zsh Development Group specifically disclaim any
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose.  The software
 * provided hereunder is on an "as is" basis, and Paul Falstad and the
 * Zsh Development Group have no obligation to provide maintenance,
 * support, updates, enhancements, or modifications.
 *
 */

#include "zsh.mdh"
#include "mem.pro"

/*
	There are two ways to allocate memory in zsh.  The first way is
	to call zalloc/zshcalloc, which call malloc/calloc directly.  It
	is legal to call realloc() or free() on memory allocated this way.
	The second way is to call zhalloc/hcalloc, which allocates memory
	from one of the memory pools on the heap stack.  Such memory pools 
	will automatically created when the heap allocation routines are
	called.  To be sure that they are freed at appropriate times
	one should call pushheap() before one starts using heaps and
	popheap() after that (when the memory allocated on the heaps since
	the last pushheap() isn't needed anymore).
	pushheap() saves the states of all currently allocated heaps and
	popheap() resets them to the last state saved and destroys the
	information about that state.  If you called pushheap() and
	allocated some memory on the heaps and then come to a place where
	you don't need the allocated memory anymore but you still want
	to allocate memory on the heap, you should call freeheap().  This
	works like popheap(), only that it doesn't free the information
	about the heap states (i.e. the heaps are like after the call to
	pushheap() and you have to call popheap some time later).

	Memory allocated in this way does not have to be freed explicitly;
	it will all be freed when the pool is destroyed.  In fact,
	attempting to free this memory may result in a core dump.

	If possible, the heaps are allocated using mmap() so that the
	(*real*) heap isn't filled up with empty zsh heaps. If mmap()
	is not available and zsh's own allocator is used, we use a simple trick
	to avoid that: we allocate a large block of memory before allocating
	a heap pool, this memory is freed again immediately after the pool
	is allocated. If there are only small blocks on the free list this
	guarantees that the memory for the pool is at the end of the memory
	which means that we can give it back to the system when the pool is
	freed.

	hrealloc(char *p, size_t old, size_t new) is an optimisation
	with a similar interface to realloc().  Typically the new size
	will be larger than the old one, since there is no gain in
	shrinking the allocation (indeed, that will confused hrealloc()
	since it will forget that the unused space once belonged to this
	pointer).  However, new == 0 is a special case; then if we
	had to allocate a special heap for this memory it is freed at
	that point.
*/

#if defined(HAVE_SYS_MMAN_H) && defined(HAVE_MMAP) && defined(HAVE_MUNMAP)

#include <sys/mman.h>

/*
 * This definition is designed to enable use of memory mapping on MacOS.
 * However, performance tests indicate that MacOS mapped regions are
 * somewhat slower to allocate than memory from malloc(), so whether
 * using this improves performance depends on details of zhalloc().
 */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(MAP_ANONYMOUS) && defined(MAP_PRIVATE)

#define USE_MMAP 1
#define MMAP_FLAGS (MAP_ANONYMOUS | MAP_PRIVATE)

#endif
#endif

#ifdef ZSH_MEM_WARNING
# ifndef DEBUG
#  define DEBUG 1
# endif
#endif


/* Make sure we align to the longest fundamental type. */
union mem_align {
    zlong l;
    double d;
};

#define H_ISIZE  sizeof(union mem_align)
#define HEAPSIZE (16384 - H_ISIZE)
/* Memory available for user data in default arena size */
#define HEAP_ARENA_SIZE (HEAPSIZE - sizeof(struct heap))
#define HEAPFREE (16384 - H_ISIZE)

/* Memory available for user data in heap h */
#define ARENA_SIZEOF(h) ((h)->size - sizeof(struct heap))

/* list of zsh heaps */

static Heap heaps;

/* a heap with free space, not always correct (it will be the last heap
 * if that was newly allocated but it may also be another one) */

static Heap fheap;

/**/
#ifdef ZSH_HEAP_DEBUG
/*
 * The heap ID we'll allocate next.
 *
 * We'll avoid using 0 as that means zero-initialised memory
 * containing a heap ID is (correctly) marked as invalid.
 */
static Heapid next_heap_id = (Heapid)1;

/*
 * The ID of the heap from which we last allocated heap memory.
 * In theory, since we carefully avoid allocating heap memory during
 * interrupts, after any call to zhalloc() or wrappers this should
 * be the ID of the heap containing the memory just returned.
 */
/**/
mod_export Heapid last_heap_id;

/*
 * Stack of heaps saved by new_heaps().
 * Assumes old_heaps() will come along and restore it later
 * (outputs an error if old_heaps() is called out of sequence).
 */
static LinkList heaps_saved;

/*
 * Debugging verbosity.  This must be set from a debugger.
 * An 'or' of bits from the enum heap_debug_verbosity.
 */
static volatile int heap_debug_verbosity;

/*
 * Generate a heap identifier that's unique up to unsigned integer wrap.
 *
 * For the purposes of debugging we won't bother trying to make a
 * heap_id globally unique, which would require checking all existing
 * heaps every time we create an ID and still wouldn't do what we
 * ideally want, which is to make sure the IDs of valid heaps are
 * different from the IDs of no-longer-valid heaps.  Given that,
 * we'll just assume that if we haven't tracked the problem when the
 * ID wraps we're out of luck.  We could change the type to a long long
 * if we wanted more room
 */

static Heapid
new_heap_id(void)
{
    return next_heap_id++;
}

/**/
#endif

/* Use new heaps from now on. This returns the old heap-list. */

/**/
mod_export Heap
new_heaps(void)
{
    Heap h;

    queue_signals();
    h = heaps;

    fheap = heaps = NULL;
    unqueue_signals();

#ifdef ZSH_HEAP_DEBUG
    if (heap_debug_verbosity & HDV_NEW) {
	fprintf(stderr, "HEAP DEBUG: heap " HEAPID_FMT
		" saved, new heaps created.\n", h->heap_id);
    }
    if (!heaps_saved)
	heaps_saved = znewlinklist();
    zpushnode(heaps_saved, h);
#endif
    return h;
}

/* Re-install the old heaps again, freeing the new ones. */

/**/
mod_export void
old_heaps(Heap old)
{
    Heap h, n;

    queue_signals();
    for (h = heaps; h; h = n) {
	n = h->next;
	DPUTS(h->sp, "BUG: old_heaps() with pushed heaps");
#ifdef ZSH_HEAP_DEBUG
	if (heap_debug_verbosity & HDV_FREE) {
	    fprintf(stderr, "HEAP DEBUG: heap " HEAPID_FMT
		    "freed in old_heaps().\n", h->heap_id);
	}
#endif
#ifdef USE_MMAP
	munmap((void *) h, h->size);
#else
	zfree(h, HEAPSIZE);
#endif
#ifdef ZSH_VALGRIND
	VALGRIND_DESTROY_MEMPOOL((char *)h);
#endif
    }
    heaps = old;
#ifdef ZSH_HEAP_DEBUG
    if (heap_debug_verbosity & HDV_OLD) {
	fprintf(stderr, "HEAP DEBUG: heap " HEAPID_FMT
		"restored.\n", heaps->heap_id);
    }
    {
	Heap myold = heaps_saved ? getlinknode(heaps_saved) : NULL;
	if (old != myold)
	{
	    fprintf(stderr, "HEAP DEBUG: invalid old heap " HEAPID_FMT
		    ", expecting " HEAPID_FMT ".\n", old->heap_id,
		    myold->heap_id);
	}
    }
#endif
    fheap = NULL;
    unqueue_signals();
}

/* Temporarily switch to other heaps (or back again). */

/**/
mod_export Heap
switch_heaps(Heap new)
{
    Heap h;

    queue_signals();
    h = heaps;

#ifdef ZSH_HEAP_DEBUG
    if (heap_debug_verbosity & HDV_SWITCH) {
	fprintf(stderr, "HEAP DEBUG: heap temporarily switched from "
		HEAPID_FMT " to " HEAPID_FMT ".\n", h->heap_id, new->heap_id);
    }
#endif
    heaps = new;
    fheap = NULL;
    unqueue_signals();

    return h;
}

/* save states of zsh heaps */

/**/
mod_export void
pushheap(void)
{
    Heap h;
    Heapstack hs;

    queue_signals();

    for (h = heaps; h; h = h->next) {
	DPUTS(!h->used && h->next, "BUG: empty heap");
	hs = (Heapstack) zalloc(sizeof(*hs));
	hs->next = h->sp;
	h->sp = hs;
	hs->used = h->used;
#ifdef ZSH_HEAP_DEBUG
	hs->heap_id = h->heap_id;
	h->heap_id = new_heap_id();
	if (heap_debug_verbosity & HDV_PUSH) {
	    fprintf(stderr, "HEAP DEBUG: heap " HEAPID_FMT " pushed, new id is "
		    HEAPID_FMT ".\n",
		    hs->heap_id, h->heap_id);
	}
#endif
    }
    unqueue_signals();
}

/* reset heaps to previous state */

/**/
mod_export void
freeheap(void)
{
    Heap h, hn, hl = NULL;

    queue_signals();

    /*
     * When pushheap() is called, it sweeps over the entire heaps list of
     * arenas and marks every one of them with the amount of free space in
     * that arena at that moment.  zhalloc() is then allowed to grab bits
     * out of any of those arenas that have free space.
     *
     * Whenever fheap is NULL here, the loop below sweeps back over the
     * entire heap list again, resetting the free space in every arena to
     * the amount stashed by pushheap() and finding the arena with the most
     * free space to optimize zhalloc()'s next search.  When there's a lot
     * of stuff already on the heap, this is an enormous amount of work,
     * and performance goes to hell.
     *
     * Therefore, we defer freeing the most recently allocated arena until
     * we reach popheap().
     *
     * However, if the arena to which fheap points is unused, we want to
     * reclaim space in earlier arenas, so we have no choice but to do the
     * sweep for a new fheap.
     */
    if (fheap && !fheap->sp)
       fheap = NULL;   /* We used to do this unconditionally */
    /*
     * In other cases, either fheap is already correct, or it has never
     * been set and this loop will do it, or it'll be reset from scratch
     * on the next popheap().  So all that's needed here is to pick up
     * the scan wherever the last pass [or the last popheap()] left off.
     */
    for (h = (fheap ? fheap : heaps); h; h = hn) {
	hn = h->next;
	if (h->sp) {
#ifdef ZSH_MEM_DEBUG
#ifdef ZSH_VALGRIND
	    VALGRIND_MAKE_MEM_UNDEFINED((char *)arena(h) + h->sp->used,
					h->used - h->sp->used);
#endif
	    memset(arena(h) + h->sp->used, 0xff, h->used - h->sp->used);
#endif
	    h->used = h->sp->used;
	    if (!fheap) {
		if (h->used < ARENA_SIZEOF(h))
		    fheap = h;
	    } else if (ARENA_SIZEOF(h) - h->used >
		       ARENA_SIZEOF(fheap) - fheap->used)
		fheap = h;
	    hl = h;
#ifdef ZSH_HEAP_DEBUG
	    /*
	     * As the free makes the heap invalid, give it a new
	     * identifier.  We're not popping it, so don't use
	     * the one in the heap stack.
	     */
	    {
		Heapid new_id = new_heap_id();
		if (heap_debug_verbosity & HDV_FREE) {
		    fprintf(stderr, "HEAP DEBUG: heap " HEAPID_FMT
			    " freed, new id is " HEAPID_FMT ".\n",
			    h->heap_id, new_id);
		}
		h->heap_id = new_id;
	    }
#endif
#ifdef ZSH_VALGRIND
	    VALGRIND_MEMPOOL_TRIM((char *)h, (char *)arena(h), h->used);
#endif
	} else {
	    if (fheap == h)
		fheap = NULL;
	    if (h->next) {
		/* We want to cut this out of the arena list if we can */
		if (h == heaps)
		    hl = heaps = h->next;
		else if (hl && hl->next == h)
		    hl->next = h->next;
		else {
		    DPUTS(hl, "hl->next != h when freeing");
		    hl = h;
		    continue;
		}
		h->next = NULL;
	    } else {
		/* Leave an empty arena at the end until popped */
		h->used = 0;
		fheap = hl = h;
		break;
	    }
#ifdef USE_MMAP
	    munmap((void *) h, h->size);
#else
	    zfree(h, HEAPSIZE);
#endif
#ifdef ZSH_VALGRIND
	    VALGRIND_DESTROY_MEMPOOL((char *)h);
#endif
	}
    }
    if (hl)
	hl->next = NULL;
    else
	heaps = fheap = NULL;

    unqueue_signals();
}

/* reset heap to previous state and destroy state information */

/**/
mod_export void
popheap(void)
{
    Heap h, hn, hl = NULL;
    Heapstack hs;

    queue_signals();

    fheap = NULL;
    for (h = heaps; h; h = hn) {
	hn = h->next;
	if ((hs = h->sp)) {
	    h->sp = hs->next;
#ifdef ZSH_MEM_DEBUG
#ifdef ZSH_VALGRIND
	    VALGRIND_MAKE_MEM_UNDEFINED((char *)arena(h) + hs->used,
					h->used - hs->used);
#endif
	    memset(arena(h) + hs->used, 0xff, h->used - hs->used);
#endif
	    h->used = hs->used;
#ifdef ZSH_HEAP_DEBUG
	    if (heap_debug_verbosity & HDV_POP) {
		fprintf(stderr, "HEAP DEBUG: heap " HEAPID_FMT
			" popped, old heap was " HEAPID_FMT ".\n",
			h->heap_id, hs->heap_id);
	    }
	    h->heap_id = hs->heap_id;
#endif
#ifdef ZSH_VALGRIND
	    VALGRIND_MEMPOOL_TRIM((char *)h, (char *)arena(h), h->used);
#endif
	    if (!fheap) {
		if (h->used < ARENA_SIZEOF(h))
		    fheap = h;
	    } else if (ARENA_SIZEOF(h) - h->used >
		       ARENA_SIZEOF(fheap) - fheap->used)
		fheap = h;
	    zfree(hs, sizeof(*hs));

	    hl = h;
	} else {
	    if (h->next) {
		/* We want to cut this out of the arena list if we can */
		if (h == heaps)
		    hl = heaps = h->next;
		else if (hl && hl->next == h)
		    hl->next = h->next;
		else {
		    DPUTS(hl, "hl->next != h when popping");
		    hl = h;
		    continue;
		}
		h->next = NULL;
	    } else if (hl == h)	/* This is the last arena of all */
		hl = NULL;
#ifdef USE_MMAP
	    munmap((void *) h, h->size);
#else
	    zfree(h, HEAPSIZE);
#endif
#ifdef ZSH_VALGRIND
	    VALGRIND_DESTROY_MEMPOOL((char *)h);
#endif
	}
    }
    if (hl)
	hl->next = NULL;
    else
	heaps = NULL;

    unqueue_signals();
}

#ifdef USE_MMAP
/*
 * Utility function to allocate a heap area of at least *n bytes.
 * *n will be rounded up to the next page boundary.
 */
static Heap
mmap_heap_alloc(size_t *n)
{
    Heap h;
    static size_t pgsz = 0;

    if (!pgsz) {

#ifdef _SC_PAGESIZE
	pgsz = sysconf(_SC_PAGESIZE);     /* SVR4 */
#else
# ifdef _SC_PAGE_SIZE
	pgsz = sysconf(_SC_PAGE_SIZE);    /* HPUX */
# else
	pgsz = getpagesize();
# endif
#endif

	pgsz--;
    }
    *n = (*n + pgsz) & ~pgsz;
    h = (Heap) mmap(NULL, *n, PROT_READ | PROT_WRITE,
		    MMAP_FLAGS, -1, 0);
    if (h == ((Heap) -1)) {
	zerr("fatal error: out of heap memory");
	exit(1);
    }

    return h;
}
#endif

/* check whether a pointer is within a memory pool */

/**/
mod_export void *
zheapptr(void *p)
{
    Heap h;
    queue_signals();
    for (h = heaps; h; h = h->next)
	if ((char *)p >= arena(h) &&
	    (char *)p + H_ISIZE < arena(h) + ARENA_SIZEOF(h))
	    break;
    unqueue_signals();
    return (h ? p : 0);
}

/* allocate memory from the current memory pool */

/**/
mod_export void *
zhalloc(size_t size)
{
    Heap h, hp = NULL;
    size_t n;
#ifdef ZSH_VALGRIND
    size_t req_size = size;

    if (size == 0)
	return NULL;
#endif

    size = (size + H_ISIZE - 1) & ~(H_ISIZE - 1);

    queue_signals();

    /* find a heap with enough free space */

    /*
     * This previously assigned:
     *   h = ((fheap && ARENA_SIZEOF(fheap) >= (size + fheap->used))
     *	      ? fheap : heaps);
     * but we think that nothing upstream of fheap has more free space,
     * so why start over at heaps just because fheap has too little?
     */
    for (h = (fheap ? fheap : heaps); h; h = h->next) {
	hp = h;
	if (ARENA_SIZEOF(h) >= (n = size + h->used)) {
	    void *ret;

	    h->used = n;
	    ret = arena(h) + n - size;
	    unqueue_signals();
#ifdef ZSH_HEAP_DEBUG
	    last_heap_id = h->heap_id;
	    if (heap_debug_verbosity & HDV_ALLOC) {
		fprintf(stderr, "HEAP DEBUG: allocated memory from heap "
			HEAPID_FMT ".\n", h->heap_id);
	    }
#endif
#ifdef ZSH_VALGRIND
	    VALGRIND_MEMPOOL_ALLOC((char *)h, (char *)ret, req_size);
#endif
	    return ret;
	}
    }
    {

	n = HEAP_ARENA_SIZE > size ? HEAPSIZE : size + sizeof(*h);

#ifdef USE_MMAP
	h = mmap_heap_alloc(&n);
#else
	h = (Heap) zalloc(n);
#endif

	h->size = n;
	h->used = size;
	h->next = NULL;
	h->sp = NULL;
#ifdef ZSH_HEAP_DEBUG
	h->heap_id = new_heap_id();
	if (heap_debug_verbosity & HDV_CREATE) {
	    fprintf(stderr, "HEAP DEBUG: create new heap " HEAPID_FMT ".\n",
		    h->heap_id);
	}
#endif
#ifdef ZSH_VALGRIND
	VALGRIND_CREATE_MEMPOOL((char *)h, 0, 0);
	VALGRIND_MAKE_MEM_NOACCESS((char *)arena(h),
				   n - ((char *)arena(h)-(char *)h));
	VALGRIND_MEMPOOL_ALLOC((char *)h, (char *)arena(h), req_size);
#endif

	DPUTS(hp && hp->next, "failed to find end of chain in zhalloc");
	if (hp)
	    hp->next = h;
	else
	    heaps = h;
	fheap = h;

	unqueue_signals();
#ifdef ZSH_HEAP_DEBUG
	last_heap_id = h->heap_id;
	if (heap_debug_verbosity & HDV_ALLOC) {
	    fprintf(stderr, "HEAP DEBUG: allocated memory from heap "
		    HEAPID_FMT ".\n", h->heap_id);
	}
#endif
	return arena(h);
    }
}

/**/
mod_export void *
hrealloc(char *p, size_t old, size_t new)
{
    Heap h, ph;

#ifdef ZSH_VALGRIND
    size_t new_req = new;
#endif

    old = (old + H_ISIZE - 1) & ~(H_ISIZE - 1);
    new = (new + H_ISIZE - 1) & ~(H_ISIZE - 1);

    if (old == new)
	return p;
    if (!old && !p)
#ifdef ZSH_VALGRIND
	return zhalloc(new_req);
#else
	return zhalloc(new);
#endif

    /* find the heap with p */

    queue_signals();
    for (h = heaps, ph = NULL; h; ph = h, h = h->next)
	if (p >= arena(h) && p < arena(h) + ARENA_SIZEOF(h))
	    break;

    DPUTS(!h, "BUG: hrealloc() called for non-heap memory.");
    DPUTS(h->sp && arena(h) + h->sp->used > p,
	  "BUG: hrealloc() wants to realloc pushed memory");

    /*
     * If the end of the old chunk is before the used pointer,
     * more memory has been zhalloc'ed afterwards.
     * We can't tell if that's still in use, obviously, since
     * that's the whole point of heap memory.
     * We have no choice other than to grab some more memory
     * somewhere else and copy in the old stuff.
     */
    if (p + old < arena(h) + h->used) {
	if (new > old) {
#ifdef ZSH_VALGRIND
	    char *ptr = (char *) zhalloc(new_req);
#else
	    char *ptr = (char *) zhalloc(new);
#endif
	    memcpy(ptr, p, old);
#ifdef ZSH_MEM_DEBUG
	    memset(p, 0xff, old);
#endif
#ifdef ZSH_VALGRIND
	    VALGRIND_MEMPOOL_FREE((char *)h, (char *)p);
	    /*
	     * zhalloc() marked h,ptr,new as an allocation so we don't
	     * need to do that here.
	     */
#endif
	    unqueue_signals();
	    return ptr;
	} else {
#ifdef ZSH_VALGRIND
	    VALGRIND_MEMPOOL_FREE((char *)h, (char *)p);
	    if (p) {
		VALGRIND_MEMPOOL_ALLOC((char *)h, (char *)p,
				       new_req);
		VALGRIND_MAKE_MEM_DEFINED((char *)h, (char *)p);
	    }
#endif
	    unqueue_signals();
	    return new ? p : NULL;
	}
    }

    DPUTS(p + old != arena(h) + h->used, "BUG: hrealloc more than allocated");

    /*
     * We now know there's nothing afterwards in the heap, now see if
     * there's nothing before.  Then we can reallocate the whole thing.
     * Otherwise, we need to keep the stuff at the start of the heap,
     * then allocate a new one too; this is handled below.  (This will
     * guarantee we occupy a full heap next time round, provided we
     * don't use the heap for anything else.)
     */
    if (p == arena(h)) {
#ifdef ZSH_HEAP_DEBUG
	Heapid heap_id = h->heap_id;
#endif
	/*
	 * Zero new seems to be a special case saying we've finished
	 * with the specially reallocated memory, see scanner() in glob.c.
	 */
	if (!new) {
	    if (ph)
		ph->next = h->next;
	    else
		heaps = h->next;
	    fheap = NULL;
#ifdef USE_MMAP
	    munmap((void *) h, h->size);
#else
	    zfree(h, HEAPSIZE);
#endif
#ifdef ZSH_VALGRIND
	    VALGRIND_DESTROY_MEMPOOL((char *)h);
#endif
	    unqueue_signals();
	    return NULL;
	}
	if (new > ARENA_SIZEOF(h)) {
	    Heap hnew;
	    /*
	     * Not enough memory in this heap.  Allocate a new
	     * one of sufficient size.
	     *
	     * To avoid this happening too often, allocate
	     * chunks in multiples of HEAPSIZE.
	     * (Historical note:  there didn't used to be any
	     * point in this since we didn't consistently record
	     * the allocated size of the heap, but now we do.)
	     */
	    size_t n = (new + sizeof(*h) + HEAPSIZE);
	    n -= n % HEAPSIZE;
	    fheap = NULL;

#ifdef USE_MMAP
	    {
		/*
		 * I don't know any easy portable way of requesting
		 * a mmap'd segment be extended, so simply allocate
		 * a new one and copy.
		 */
		hnew = mmap_heap_alloc(&n);
		/* Copy the entire heap, header (with next pointer) included */
		memcpy(hnew, h, h->size);
		munmap((void *)h, h->size);
	    }
#else
	    hnew = (Heap) realloc(h, n);
#endif
#ifdef ZSH_VALGRIND
	    VALGRIND_MEMPOOL_FREE((char *)h, p);
	    VALGRIND_DESTROY_MEMPOOL((char *)h);
	    VALGRIND_CREATE_MEMPOOL((char *)hnew, 0, 0);
	    VALGRIND_MEMPOOL_ALLOC((char *)hnew, (char *)arena(hnew),
				   new_req);
	    VALGRIND_MAKE_MEM_DEFINED((char *)hnew, (char *)arena(hnew));
#endif
	    h = hnew;

	    h->size = n;
	    if (ph)
		ph->next = h;
	    else
		heaps = h;
	}
#ifdef ZSH_VALGRIND
	else {
	    VALGRIND_MEMPOOL_FREE((char *)h, (char *)p);
	    VALGRIND_MEMPOOL_ALLOC((char *)h, (char *)p, new_req);
	    VALGRIND_MAKE_MEM_DEFINED((char *)h, (char *)p);
	}
#endif
	h->used = new;
#ifdef ZSH_HEAP_DEBUG
	h->heap_id = heap_id;
#endif
	unqueue_signals();
	return arena(h);
    }
#ifndef USE_MMAP
    DPUTS(h->used > ARENA_SIZEOF(h), "BUG: hrealloc at invalid address");
#endif
    if (h->used + (new - old) <= ARENA_SIZEOF(h)) {
	h->used += new - old;
	unqueue_signals();
#ifdef ZSH_VALGRIND
	VALGRIND_MEMPOOL_FREE((char *)h, (char *)p);
	VALGRIND_MEMPOOL_ALLOC((char *)h, (char *)p, new_req);
	VALGRIND_MAKE_MEM_DEFINED((char *)h, (char *)p);
#endif
	return p;
    } else {
	char *t = zhalloc(new);
	memcpy(t, p, old > new ? new : old);
	h->used -= old;
#ifdef ZSH_MEM_DEBUG
	memset(p, 0xff, old);
#endif
#ifdef ZSH_VALGRIND
	VALGRIND_MEMPOOL_FREE((char *)h, (char *)p);
	/* t already marked as allocated by zhalloc() */
#endif
	unqueue_signals();
	return t;
    }
}

/**/
#ifdef ZSH_HEAP_DEBUG
/*
 * Check if heap_id is the identifier of a currently valid heap,
 * including any heap buried on the stack, or of permanent memory.
 * Return 0 if so, else 1.
 *
 * This gets confused by use of switch_heaps().  That's because so do I.
 */

/**/
mod_export int
memory_validate(Heapid heap_id)
{
    Heap h;
    Heapstack hs;
    LinkNode node;

    if (heap_id == HEAPID_PERMANENT)
	return 0;

    queue_signals();
    for (h = heaps; h; h = h->next) {
	if (h->heap_id == heap_id) {
	    unqueue_signals();
	    return 0;
	}
	for (hs = heaps->sp; hs; hs = hs->next) {
	    if (hs->heap_id == heap_id) {
		unqueue_signals();
		return 0;
	    }
	}
    }

    if (heaps_saved) {
	for (node = firstnode(heaps_saved); node; incnode(node)) {
	    for (h = (Heap)getdata(node); h; h = h->next) {
		if (h->heap_id == heap_id) {
		    unqueue_signals();
		    return 0;
		}
		for (hs = heaps->sp; hs; hs = hs->next) {
		    if (hs->heap_id == heap_id) {
			unqueue_signals();
			return 0;
		    }
		}
	    }
	}
    }

    unqueue_signals();
    return 1;
}
/**/
#endif

/* allocate memory from the current memory pool and clear it */

/**/
mod_export void *
hcalloc(size_t size)
{
    void *ptr;

    ptr = zhalloc(size);
    memset(ptr, 0, size);
    return ptr;
}

/* allocate permanent memory */

/**/
mod_export void *
zalloc(size_t size)
{
    void *ptr;

    if (!size)
	size = 1;
    queue_signals();
    if (!(ptr = (void *) malloc(size))) {
	zerr("fatal error: out of memory");
	exit(1);
    }
    unqueue_signals();

    return ptr;
}

/**/
mod_export void *
zshcalloc(size_t size)
{
    void *ptr = zalloc(size);
    if (!size)
	size = 1;
    memset(ptr, 0, size);
    return ptr;
}

/* This front-end to realloc is used to make sure we have a realloc *
 * that conforms to POSIX realloc.  Older realloc's can fail if     *
 * passed a NULL pointer, but POSIX realloc should handle this.  A  *
 * better solution would be for configure to check if realloc is    *
 * POSIX compliant, but I'm not sure how to do that.                */

/**/
mod_export void *
zrealloc(void *ptr, size_t size)
{
    queue_signals();
    if (ptr) {
	if (size) {
	    /* Do normal realloc */
	    if (!(ptr = (void *) realloc(ptr, size))) {
		zerr("fatal error: out of memory");
		exit(1);
	    }
	    unqueue_signals();
	    return ptr;
	}
	else
	    /* If ptr is not NULL, but size is zero, *
	     * then object pointed to is freed.      */
	    free(ptr);

	ptr = NULL;
    } else {
	/* If ptr is NULL, then behave like malloc */
        if (!(ptr = (void *) malloc(size))) {
            zerr("fatal error: out of memory");
            exit(1);
        }
    }
    unqueue_signals();

    return ptr;
}

/**/
mod_export void
zfree(void *p, UNUSED(int sz))
{
    free(p);
}

/**/
mod_export void
zsfree(char *p)
{
    free(p);
}


#ifndef AGNOSTIC_ALLOCATE_H_
#define AGNOSTIC_ALLOCATE_H_

#ifdef __KERNEL__
#define agnostic_zallocate(sz) kzalloc(sz, GFP_KERNEL)
#define agnostic_free(ptr) kfree(ptr)
#else
#define agnostic_zallocate(sz), calloc(sz, 1)
#define agnostic_free(ptr) free(ptr)
#endif


#endif /* AGNOSTIC_ALLOCATE_H_ */

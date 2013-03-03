/*
 * Copyright (c) 2010, the Short-term Memory Project Authors.
 * All rights reserved. Please see the AUTHORS file for details.
 * Use of this source code is governed by a BSD license that
 * can be found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "stm.h"
#include "scm-desc.h"
#include "regmalloc.h"
#include "meter.h"
#include "arch.h"

/**
 * Recycles a region in O(1) by pooling
 * the list of free region_pages except the
 * first region page iff the region_page_pool
 * limit is not exceeded, otherwise the region_pages
 * except the first one are deallocated and
 * the memory is handed back to the OS in O(n),
 * n = amount of region pages - 1.
 *
 * The remaining first region page indicates that the region
 * once existed, which is necessary to differentiate
 * it from regions which have not yet been used.
 * This indicates how many not-yet-used regions
 * are available.
 * Returns if no or just one region_page
 * has been allocated in the region.
 */
static void recycle_region(region_t* region);

/**
 * Recycles a region in O(1) by pooling
 * the list of free region_pages except the
 * first region page iff the region_page_pool
 * limit is not exceeded, otherwise the region_pages
 * except the first one are deallocated and
 * the memory is handed back to the OS in O(n),
 * n = amount of region pages - 1.
 *
 * The remaining first region page indicates that the region
 * once existed, which is necessary to differentiate
 * it from regions which have not yet been used.
 * This indicates how many not-yet-used regions
 * are available.
 *
 * If the region was unregistered, all region pages
 * are recycled or deallocated.
 */
static void recycle_region(region_t* region) {

#ifdef SCM_DEBUG
    printf("Recycle region: %p \n", region);
#endif

// check pre-conditions
#ifdef SCM_CHECK_CONDITIONS
    if (region == NULL) {
        fprintf(stderr, "Region recycling failed: NULL region should not appear in the descriptor buffers.");
        exit(-1);
    } else if (region->firstPage == NULL || region->lastPage == NULL) {
        fprintf(stderr, "Region recycling failed: Descriptor points to a region which was not correctly initialized.");
        exit(-1);
    }
    if (region->dc != 0) {
        fprintf(stderr, "Region recycling failed: Region seems to be still alive.");
        exit(-1);
    }
    region_t* invar_region = region;
#endif

    region_page_t* legacy_pages;
    unsigned long number_of_recycle_region_pages;

    // if the region has been used in the current thread...
    if (region->age == descriptor_root->current_time) {
        //.. recycle everything except the first page
        region_page_t* firstPage = region->firstPage;
        legacy_pages = firstPage->nextPage;

        memset(firstPage, 0, REGION_PAGE_SIZE);
        region->last_address_in_last_page =
        		(unsigned long)&firstPage->memory + REGION_PAGE_PAYLOAD_SIZE-1;

        // nothing to put into the pool
        if (legacy_pages == NULL) {

// check post-conditions
#ifdef SCM_CHECK_CONDITIONS
            if (region->number_of_region_pages != 1) {
                fprintf(stderr, "Region recycling failed: Number of region pages is %u, but only one "
                		"region page exists\n",
                		region->number_of_region_pages);
                exit(-1);
            } else {
                if (region->firstPage != region->lastPage) {
                    fprintf(stderr, "Region recycling failed: Last region page is not equal to first "
                    		"region page, but only one region page exists\n");
                    exit(-1);
                }
                if (region != invar_region) {
                	fprintf(stderr, "Region recycling failed: The region changed during recycling\n");
                	exit(-1);
                }
                if(region->firstPage->nextPage != NULL) {
                	fprintf(stderr, "Region recycling failed: Next page pointer is corrupt: %p\n",
                			region->firstPage->nextPage);
                	exit(-1);
                }
            }
#endif
            region->dc = 0;

            return;
        }


// check post-conditions
#ifdef SCM_CHECK_CONDITIONS
        if (region->number_of_region_pages <= 1) {
			fprintf(stderr, "Region recycling failed: Number of region pages is %u, "
                    "but more than 1 region pages were expected.\n",
					region->number_of_region_pages);
			exit(-1);
		}
#endif

        number_of_recycle_region_pages =
            region->number_of_region_pages - 1;

    }
    // if the region was a zombie in the current thread...
    else {
#ifdef SCM_DEBUG
        printf("Region expired\n");
#endif
        //.. recycle everything, also the first page
        legacy_pages = region->firstPage;

        // nothing to put into the pool
        if (legacy_pages == NULL) {


// check post-conditions
#ifdef SCM_CHECK_CONDITIONS
            if (region->number_of_region_pages != 0) {
                fprintf(stderr, "Region recycling failed: "
                        "Number of region pages is not zero, but no region pages exist\n");
                exit(-1);
            } else {
                if (region->firstPage != region->lastPage) {
                	fprintf(stderr, "Region recycling failed: "
                            "Last region page is not equal to first region page, "
                            "but only one region page exists\n");
                    exit(-1);
                }
                if (region != invar_region) {
                	fprintf(stderr, "Region recycling failed: The region changed during recycling\n");
                    exit(-1);
                }
            }
#endif
            region->dc = 0;

            return;
        }

// check post-conditions
#ifdef SCM_CHECK_CONDITIONS
		if (region->number_of_region_pages == 0) {
			fprintf(stderr, "Region recycling failed: "
                    "Number of region pages is %u, but legacy pages "
					"could be obtained\n",
					region->number_of_region_pages);
			exit(-1);
		}
#endif

        // a zombie region recycles all region pages
        number_of_recycle_region_pages =
            region->number_of_region_pages;
    }

    unsigned long number_of_pooled_region_pages =
        descriptor_root->number_of_pooled_region_pages;

    // is there space in the region page pool?
    if ((number_of_pooled_region_pages
            + number_of_recycle_region_pages) <=
            SCM_REGION_PAGE_FREELIST_SIZE) {
    	//..yes, there is space in the region page pool

#ifdef SCM_PRINTMEM
        region_page_t* p = legacy_pages;
        unsigned long pooled_memory;

        while(p != NULL) {
        	inc_pooled_mem(REGION_PAGE_SIZE);
            dec_needed_mem(p->used_memory);
        	p = p->nextPage;
        }
#endif
#ifdef SCM_PRINTOVERHEAD
        region_page_t* p2 = legacy_pages;
        
        while(p2 != NULL) {
            inc_overhead(__real_malloc_usable_size(p2));
        	p2 = p2->nextPage;
        }
#endif

        region_page_t* first_in_pool = descriptor_root->region_page_pool;
        region_page_t* last_page = region->lastPage;

        last_page->nextPage = first_in_pool;
        descriptor_root->region_page_pool = legacy_pages;
        descriptor_root->number_of_pooled_region_pages =
			number_of_pooled_region_pages + number_of_recycle_region_pages;

    } else {

    	//..no, there is no space in the region page pool
        // If the first region page is not NULL,
        // deallocate all region pages
        region_page_t* page2free = legacy_pages;

        while (page2free != NULL && (number_of_pooled_region_pages
				+ number_of_recycle_region_pages) >
				SCM_REGION_PAGE_FREELIST_SIZE) {
#ifdef SCM_PRINTMEM
            inc_freed_mem(REGION_PAFE_SIZE);
#endif
            region_page_t* next = page2free->nextPage;
            __real_free(page2free);
            page2free = next;

            number_of_recycle_region_pages--;
        }


        // check again if we can recycle now
		if (page2free != NULL && (number_of_pooled_region_pages
				+ number_of_recycle_region_pages) <=
				SCM_REGION_PAGE_FREELIST_SIZE) {
            
            region_page_t* first_in_pool = descriptor_root->region_page_pool;
			region_page_t* last_page = region->lastPage;
			if(last_page != NULL) {
				last_page->nextPage = first_in_pool;
				descriptor_root->region_page_pool = page2free;
				descriptor_root->number_of_pooled_region_pages =
						number_of_recycle_region_pages - 1;

#ifdef SCM_PRINTMEM
				inc_pooled_mem(number_of_recycle_region_pages * REGION_PAGE_SIZE);
#endif
			}
		}

		descriptor_root->number_of_pooled_region_pages =
				number_of_pooled_region_pages + number_of_recycle_region_pages;
    }

    region->number_of_region_pages = 1;
    region->lastPage = region->firstPage;
    region->last_address_in_last_page = (unsigned long)
    		&region->lastPage->memory + REGION_PAGE_PAYLOAD_SIZE;
    region->next_free_address = &region->lastPage->memory;

// check post-conditions
#ifdef SCM_CHECK_CONDITIONS
        if (region->number_of_region_pages != 1) {
            fprintf(stderr, "Region recycling failed: "
                    "Number of region pages is %u, but only one region page exists.\n", 
                    region->number_of_region_pages);
            exit(-1);
        } else {
            if (region->firstPage != region->lastPage) {
                fprintf(stderr, "Region recycling failed: "
                    "Last region page is not equal to first region page, "
                    "but only one region page should exist.\n");
                exit(-1);
            }
            if (region != invar_region) {
                fprintf(stderr, "Region recycling failed: "
                    "The region changed during recycling.\n");
                exit(-1);
            }
        }
#endif
    if (region->age != descriptor_root->current_time) {

        region->number_of_region_pages = 0;
        region->lastPage = region->firstPage = NULL;

// check post-conditions
#ifdef SCM_CHECK_CONDITIONS
        if (region->number_of_region_pages != 0) {
            fprintf(stderr, "Region recycling failed: "
                    "Number of region pages is %u, but no region pages should exist.\n", 
                    region->number_of_region_pages);
            exit(-1);
        } else {
            if (region->firstPage != NULL) {
                fprintf(stderr, "Region recycling failed: "
                        "First page is not null as expected\n");
                exit(-1);
            }
            if (region != invar_region) {
                fprintf(stderr, "Region recycling failed: "
                        "The region changed during recycling\n");
                exit(-1);
            }
        }
#endif
    }
}

/*
 * Expires a region descriptor and decrements its descriptor counter. When the
 * descriptor counter is 0, the region to which the descriptor points to is recycled.
 * Returns 0 iff no more expired region descriptors exist.
 */
int expire_reg_descriptor_if_exists(expired_descriptor_page_list_t *list) {

// check pre-conditions
#ifdef SCM_CHECK_CONDITIONS
    if (list == NULL) {
        perror("Expired descriptor page list is NULL, but was expected to exist");
        return 0;
    }
#endif

    region_t* expired_region = (region_t*) get_expired_mem(list);

    if (expired_region != NULL) {
        if (atomic_int_dec_and_test((volatile int*) & expired_region->dc)) {

#ifdef SCM_DEBUG
            printf("region FREE(%lx)\n",
                   (unsigned long) expired_region);
#endif

            recycle_region(expired_region);

// optimization: avoiding else conditions
#ifdef SCM_DEBUG
        } else {

            printf("decrementing DC==%lu\n", expired_region->dc);
#endif
        }
        return 1;
    } else {
#ifdef SCM_DEBUG
        printf("no expired object found\n");
#endif
        return 0;
    }
}
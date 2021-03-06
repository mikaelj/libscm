/*
 * Copyright (c) 2010, the Short-term Memory Project Authors.
 * All rights reserved. Please see the AUTHORS file for details.
 * Use of this source code is governed by a BSD license that
 * can be found in the LICENSE file.
 */

#ifndef _ARCH_H_
#define	_ARCH_H_

#define atomic_int_inc(atomic) (atomic_int_add((atomic), 1))

#define atomic_int_dec_and_test(atomic)	\
 (atomic_int_exchange_and_add((atomic), -1) == 1)

static inline void toggle_bit_at_pos(int *bitmap, int pos) {
    *bitmap = *bitmap ^ (1 << pos);
}

#if defined __i386__ || defined __x86_64__

static inline unsigned long long rdtsc(void) {
    unsigned hi, lo;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long) lo) | (((unsigned long long) hi) << 32);
}

/* 32bit bit-map operations */

/* bit scan forward returns the index of the LEAST significant bit
 * or -1 if bitmap==0 */
static inline int bsfl(int bitmap) {

    int result;

    __asm__ ("bsfl %1, %0;"      /* Bit Scan Forward                         */
             "jnz 1f;"           /* if(ZF==1) invalid input of 0; jump to 1: */
             "movl $-1, %0;"     /* set output to error -1                   */
             "1:"                /* jump label for line 2                    */
            : "=r" (result)
            : "g" (bitmap)
            );
    return result;
}

/* bit scan reverse returns the index of the MOST significant bit
 * or -1 if bitmap==0 */
static inline int bsrl(int bitmap) {

    int result;

    __asm__("bsrl %1, %0;"
            "jnz 1f;"
            "movl $-1, %0;"
            "1:"
            : "=r" (result)
            : "g" (bitmap)
            );
    return result;
}

/*code adapted from glib http://ftp.gnome.org/pub/gnome/sources/glib/2.24/
 * g_atomic_*: atomic operations.
 * Copyright (C) 2003 Sebastian Wilhelmi
 * Copyright (C) 2007 Nokia Corporation
 */
static inline int atomic_int_exchange_and_add(volatile int *atomic,
        int val) {

    int result;

    __asm__ __volatile__("lock; xaddl %0,%1"
            : "=r" (result), "=m" (*atomic)
            : "0" (val), "m" (*atomic));
    return result;
}

static inline void atomic_int_add(volatile int *atomic, int val) {
    __asm__ __volatile__("lock; addl %1,%0"
            : "=m" (*atomic)
            : "ir" (val), "m" (*atomic));
}

static inline int atomic_int_compare_and_exchange(volatile int *atomic,
        int oldval, int newval) {

    int result;

    __asm__ __volatile__("lock; cmpxchgl %2, %1"
            : "=a" (result), "=m" (*atomic)
            : "r" (newval), "m" (*atomic), "0" (oldval)
            );

    return result;
}

#endif /* defined __i386__ || defined __x86_64__ */

#endif	/* _ARCH_H_ */

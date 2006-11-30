/*
 * $Id$
 * 
 * This file has been modified as part of the FreeMiNT project. See
 * the file Changes.MH for details and dates.
 */

/*
 * Copyright 1992 Eric R. Smith.
 * Copyright 1993 Atari Corporation.
 * All rights reserved.
 */

# ifndef _mint_m68k_misc_h
# define _mint_m68k_misc_h

# include "mint/kcompiler.h"


/*
 * note that we must save some registers ourselves,
 * or else gcc will run out of reggies to use
 * and complain
 */

#if __GNUC__ > 2 || __GNUC_MINOR__ > 5
# if __GNUC__ >= 3
   /* gcc 3 does not want a clobbered register to be input or output */
#  define MINT_CLOBBER_LIST	"d1", "d2", "d3", "d4", "a0", "a1", "a2", "a3"
# else	
#  define MINT_CLOBBER_LIST	"d0", "d1", "d2", "d3", "d4", "a0", "a1", "a2", "a3"
/* old version
	__CLOBBER_RETURN("d0")		\
	  "d1", "d2", "d3", "d4",		\
	  "a0", "a1", "a2", "a3"
*/
# endif
#else
# define MINT_CLOBBER_LIST
#endif

/* On TOS 1.04, when calling Bconout(2,'\a') the VDI jumps directly
   back to the BIOS which expects the register A5 to be set to zero.
   (Specifying the register as clobbered does not work.) */

# define callout1(func, a)			\
({						\
	register long retvalue __asm__("d0");	\
	long _f = func;				\
	short _a = (short)(a);			\
						\
	__asm__ volatile			\
	("  moveml d5-d7/a4-a6,sp@-;		\
	    movew %2,sp@-;			\
	    movel %1,a0;			\
	    subal a5,a5;			\
	    jsr a0@;				\
	    addqw #2,sp;			\
	    moveml sp@+,d5-d7/a4-a6 "		\
	: "=r"(retvalue)	/* outputs */	\
	: "r"(_f), "r"(_a)	/* inputs */	\
	:  MINT_CLOBBER_LIST /* clobbered regs */ \
	);					\
	retvalue;				\
})

# define callout2(func, a, b)			\
({						\
	register long retvalue __asm__("d0");	\
	long _f = func;				\
	short _a = (short)(a);			\
	short _b = (short)(b);			\
						\
	__asm__ volatile			\
	("  moveml d5-d7/a4-a6,sp@-;		\
	    movew %3,sp@-;			\
	    movew %2,sp@-;			\
	    movel %1,a0;			\
	    subal a5,a5;			\
	    jsr a0@;				\
	    addqw #4,sp;			\
	    moveml sp@+,d5-d7/a4-a6 "		\
	: "=r"(retvalue)	/* outputs */	\
	: "r"(_f), "r"(_a), "r"(_b) /* inputs */ \
	: MINT_CLOBBER_LIST /* clobbered regs */ \
	);					\
	retvalue;				\
})


# endif /* _mint_m68k_misc_h */

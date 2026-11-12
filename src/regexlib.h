#ifndef _REGEXLIB_H_
#define _REGEXLIB_H_

#ifdef USE_REGEXP_PCRE2POSIX
# include <pcre2posix.h>
#elif defined(USE_REGEXP)
# include <sys/types.h>
# include <regex.h>
#endif

#endif /* _REGEXLIB_H_ */

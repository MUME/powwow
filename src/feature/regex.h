#ifndef _FEATURE_REGEX_H_
#define _FEATURE_REGEX_H_

#ifdef USE_REGEXP_PCRE2POSIX
# include <pcre2posix.h>
#elif defined(USE_REGEXP)
# include <sys/types.h>
# include <regex.h>
#endif

#endif /* _FEATURE_REGEX_H_ */

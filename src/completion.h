#ifndef _COMPLETION_H_
#define _COMPLETION_H_

#include <stdbool.h>

/*
 * print the script that completes the command line of powwow in 'shell',
 * bash or zsh, whose options are the NULL-terminated 'options', such as
 * "--config" and "--config="; return false if there is none for 'shell'
 */
bool print_completion(const char *shell, const char *const *options);

#endif /* _COMPLETION_H_ */

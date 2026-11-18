/*
 *  completion.c  --  the completion of the command line of powwow in bash
 *                    and zsh, which powwow --completion prints
 *
 *  Copyright (C) the powwow authors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <string.h>

#include "completion.h"
#include "tls.h"

/*
 * the functions of the completion, the same in both shells: they take the
 * words from COMP_LINE, as bash sets it, and zsh_functions in zsh, and put
 * the space after a completed word themselves, as bash 3.2, which macOS
 * has, cannot leave it out after = only
 */
static const char *const functions[] = {
    "# add the words of $2 that start with $3 to COMPREPLY, after\n",
    "# $1, with a space after each but those that end with =, : or /\n",
    "_powwow_add()\n",
    "{\n",
    "    local word\n",
    "    for word in $2; do\n",
    "        case $word in\n",
    "            \"$3\"*) ;;\n",
    "            *) continue ;;\n",
    "        esac\n",
    "        case $word in\n",
    "            *=|*:|*/) COMPREPLY+=(\"$1$word\") ;;\n",
    "            *) COMPREPLY+=(\"$1$word \") ;;\n",
    "        esac\n",
    "    done\n",
    "}\n",
    "\n",
    "# add the files that start with $2, in the folder $3 or else\n",
    "# here, and end with $4, if given, after $1: quoted, and the\n",
    "# folders with a / after them; in zsh, by its _files, as for\n",
    "# other commands, which also offers any file if none ends\n",
    "# with $4\n",
    "_powwow_files()\n",
    "{\n",
    "    if [ -n \"${ZSH_VERSION-}\" ]; then\n",
    "        _powwow_zsh _powwow_zsh_files \"$@\"\n",
    "        return\n",
    "    fi\n",
    "    local start=\"$2\" home= file name\n",
    "    case $start in\n",
    "        \"~/\"*) home=1 start=\"$HOME/${start#\\~/}\" ;;\n",
    "    esac\n",
    "    for file in \"${3:+$3/}$start\"*; do\n",
    "        [ -e \"$file\" ] || continue\n",
    "        name=\"${file#\"${3:+$3/}\"}\"\n",
    "        [ -n \"$home\" ] && name=\"~/${name#\"$HOME/\"}\"\n",
    "        name=\"${name// /\\\\ }\"\n",
    "        if [ -d \"$file\" ]; then\n",
    "            COMPREPLY+=(\"$1$name/\")\n",
    "        else\n",
    "            case $file in\n",
    "                *\"$4\") COMPREPLY+=(\"$1$name \") ;;\n",
    "            esac\n",
    "        fi\n",
    "    done\n",
    "}\n",
    "\n",
    "# add the hosts that start with $1\n",
    "_powwow_hosts()\n",
    "{\n",
    "    if [ -n \"${ZSH_VERSION-}\" ]; then\n",
    "        _powwow_zsh _hosts\n",
    "    else\n",
    "        _powwow_add \"\" \"$(compgen -A hostname -- \"$1\")\" \"$1\"\n",
    "    fi\n",
    "}\n",
    "\n",
    "_powwow()\n",
    "{\n",
    "    [ -n \"${ZSH_VERSION-}\" ] && emulate -L sh\n",
    "    local line=\"${COMP_LINE:0:$COMP_POINT}\"\n",
    "    local cur=\"${line##*[ \t]}\"\n",
    "    local arg= args=-1 word\n",
    "    COMPREPLY=()\n",
    "    # the words before, the first of which is powwow\n",
    "    set -f\n",
    "    for word in ${line%\"$cur\"}; do\n",
    "        if [ -n \"$arg\" ]; then\n",
    "            arg=\n",
    "            continue\n",
    "        fi\n",
    "        case $word in\n",
    "            --completion) arg=$word ;;\n",
    "            -*) ;;\n",
    "            *) args=$((args + 1)) ;;\n",
    "        esac\n",
    "    done\n",
    "    set +f\n",
    "    case $arg in\n",
    "        --completion) _powwow_add \"\" \"$_powwow_shells\" \"$cur\" ;;\n",
    "        *)\n",
    "            case $cur in\n",
    "                --completion=*)\n",
    "                    _powwow_add --completion= \"$_powwow_shells\""
    " \"${cur#*=}\" ;;\n",
    "                --tls=*)\n",
    "                    word=\"${cur##*[=:]}\"\n",
    "                    _powwow_add \"${cur%\"$word\"}\" \"$_powwow_tls\""
    " \"$word\" ;;\n",
    "                --config=*) _powwow_files --config= \"${cur#*=}\" ;;\n",
    "                -*) _powwow_add \"\" \"$_powwow_options\" \"$cur\" ;;\n",
    "                *)\n",
    "                    # a definition file, also one in\n",
    "                    # $POWWOWDIR, which powwow looks in\n",
    "                    # first: one that ends with .powwow, as\n",
    "                    # the launcher names them, or else any\n",
    "                    # file; then the host\n",
    "                    if [ \"$args\" -eq 0 ]; then\n",
    "                        _powwow_definition_files \"$cur\"\n",
    "                    elif [ \"$args\" -eq 1 ]; then\n",
    "                        _powwow_hosts \"$cur\"\n",
    "                    fi ;;\n",
    "            esac ;;\n",
    "    esac\n",
    "    # bash ends words also at = and :, and replaces only the\n",
    "    # part of the word after them, its $2\n",
    "    if [ -n \"${BASH_VERSION-}\" ]; then\n",
    "        word=\"${cur%\"$2\"}\"\n",
    "        [ -n \"$word\" ] && COMPREPLY=(\"${COMPREPLY[@]#\"$word\"}\")\n",
    "    fi\n",
    "    return 0\n",
    "}\n",
    "\n",
    "# add the definition files that start with $1\n",
    "_powwow_definition_files()\n",
    "{\n",
    "    _powwow_files \"\" \"$1\" \"\" .powwow\n",
    "    case $1 in\n",
    "        */*) ;;\n",
    "        *) [ -n \"${POWWOWDIR-}\" ] \\\n",
    "               && _powwow_files \"\" \"$1\" \"$POWWOWDIR\" .powwow ;;\n",
    "    esac\n",
    "    [ -n \"${ZSH_VERSION-}\" ] && return\n",
    "    local word\n",
    "    for word in \"${COMPREPLY[@]}\"; do\n",
    "        case $word in\n",
    "            */) ;;\n",
    "            *) return ;;\n",
    "        esac\n",
    "    done\n",
    "    COMPREPLY=()\n",
    "    _powwow_files \"\" \"$1\"\n",
    "}\n",
    NULL
};

/*
 * the functions of zsh, which runs _powwow itself, by compdef, and has it
 * complete files and hosts by its own completers, which quote and expand
 * ~ as they do for other commands
 */
static const char *const zsh_functions[] = {
    "# zsh: run _powwow on the line up to the cursor, as bash would,\n",
    "# and add its words, with a space after those that end with one\n",
    "_powwow_zsh_complete()\n",
    "{\n",
    "    local COMP_LINE=\"${(j: :)words[1,CURRENT-1]} $PREFIX\"\n",
    "    local COMP_POINT=${#COMP_LINE}\n",
    "    local -a COMPREPLY\n",
    "    _powwow powwow \"$PREFIX\" \"\"\n",
    "    compadd -S ' ' -- ${${(M)COMPREPLY:#* }% }\n",
    "    compadd -S '' -- ${COMPREPLY:#* }\n",
    "    (( compstate[nmatches] > 0 ))\n",
    "}\n",
    "\n",
    "# run \"$@\", a completer of zsh, with the options of its completers\n",
    "_powwow_zsh()\n",
    "{\n",
    "    emulate -L zsh\n",
    "    setopt $_comp_options\n",
    "    \"$@\"\n",
    "}\n",
    "\n",
    "# the files of _powwow_files, by zsh's _files\n",
    "_powwow_zsh_files()\n",
    "{\n",
    "    local -a opts\n",
    "    [[ -n $1 ]] && compset -P '*='\n",
    "    [[ -n $3 ]] && opts+=(-W \"$3\")\n",
    "    [[ -n $4 ]] && opts+=(-g \"*$4(-.)\")\n",
    "    _files \"${opts[@]}\"\n",
    "}\n",
    "\n",
    "compdef _powwow_zsh_complete powwow\n",
    NULL
};

/* print "_powwow_NAME='WORDS'", of the NULL-terminated 'words' */
static void print_words(const char *name, const char *const *words)
{
    printf("_powwow_%s='", name);
    for (int i = 0; words[i] != NULL; ++i)
        printf("%s%s", i > 0 ? " " : "", words[i]);
    printf("'\n");
}

bool print_completion(const char *shell, const char *const *options)
{
    bool zsh = strcmp(shell, "zsh") == 0;
    if (!zsh && strcmp(shell, "bash") != 0)
        return false;
    printf("# the completion of the command line of powwow in %s; load it"
           " with\n"
           "#     eval \"$(powwow --completion %s)\"\n"
           "# in ~/.%src%s\n\n",
           shell, shell, shell, zsh ? ", after compinit" : "");
    print_words("options", options);
    print_words("tls", tls_option_names);
    printf("_powwow_shells='bash zsh'\n\n");
    for (int i = 0; functions[i] != NULL; ++i)
        fputs(functions[i], stdout);
    if (zsh) {
        for (int i = 0; zsh_functions[i] != NULL; ++i)
            fputs(zsh_functions[i], stdout);
    } else {
        printf("\ncomplete -o nospace -F _powwow powwow\n");
    }
    return true;
}

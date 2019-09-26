#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "wtst.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "tmux.h"

size_t complete_hint(struct window_pane *, const wchar_t *, wchar_t const ***);
int last_word(struct grid_handle *, wchar_t **);

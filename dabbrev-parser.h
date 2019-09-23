#define _GNU_SOURCE
#include "wtst.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "tmux.h"

int complete_hint(struct window_pane *, const wchar_t *, wchar_t ***);

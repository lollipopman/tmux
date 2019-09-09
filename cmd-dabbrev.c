/* $OpenBSD$ */

/*
 * Copyright (c) 2009 Jonathan Alvarado <radobobo@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Write the entire contents of a pane to a buffer or stdout.
 */

static enum cmd_retval cmd_dabbrev_exec(struct cmd *, struct cmdq_item *);

/* static char *cmd_dabbrev_append(char *, size_t *, char *, size_t); */
/* static char *cmd_dabbrev_history(struct args *, struct cmdq_item *, */
/*                                  struct window_pane *, size_t *); */

const struct cmd_entry cmd_dabbrev_entry = {
    .name = "dabbrev",
    .alias = "dabbrev",

    .args = {"ab:CeE:JNpPqS:t:", 0, 0},
    .usage = "[-aCeJNpPq] " CMD_BUFFER_USAGE " [-E end-line] "
             "[-S start-line] " CMD_TARGET_PANE_USAGE,

    .target = {'t', CMD_FIND_PANE, 0},

    .flags = CMD_AFTERHOOK,
    .exec = cmd_dabbrev_exec};

/* static char *cmd_dabbrev_append(char *buf, size_t *len, char *line, */
/*                                 size_t linelen) { */
/*   buf = xrealloc(buf, *len + linelen + 1); */
/*   memcpy(buf + *len, line, linelen); */
/*   *len += linelen; */
/*   return (buf); */
/* } */

/* static char *cmd_dabbrev_history(struct args *args, struct cmdq_item *item,
 */
/*                                  struct window_pane *wp, size_t *len) { */
/*   struct grid *gd; */
/*   const struct grid_line *gl; */
/*   struct grid_cell *gc = NULL; */
/*   int n, with_codes, escape_c0, join_lines, no_trim; */
/*   u_int i, sx, top, bottom, tmp; */
/*   char *cause, *buf, *line; */
/*   const char *Sflag, *Eflag; */
/*   size_t linelen; */
/*  */
/*   sx = screen_size_x(&wp->base); */
/*   if (args_has(args, 'a')) { */
/*     gd = wp->saved_grid; */
/*     if (gd == NULL) { */
/*       if (!args_has(args, 'q')) { */
/*         cmdq_error(item, "no alternate screen"); */
/*         return (NULL); */
/*       } */
/*       return (xstrdup("")); */
/*     } */
/*   } else */
/*     gd = wp->base.grid; */
/*  */
/*   Sflag = args_get(args, 'S'); */
/*   if (Sflag != NULL && strcmp(Sflag, "-") == 0) */
/*     top = 0; */
/*   else { */
/*     n = args_strtonum(args, 'S', INT_MIN, SHRT_MAX, &cause); */
/*     if (cause != NULL) { */
/*       top = gd->hsize; */
/*       free(cause); */
/*     } else if (n < 0 && (u_int)-n > gd->hsize) */
/*       top = 0; */
/*     else */
/*       top = gd->hsize + n; */
/*     if (top > gd->hsize + gd->sy - 1) */
/*       top = gd->hsize + gd->sy - 1; */
/*   } */
/*  */
/*   Eflag = args_get(args, 'E'); */
/*   if (Eflag != NULL && strcmp(Eflag, "-") == 0) */
/*     bottom = gd->hsize + gd->sy - 1; */
/*   else { */
/*     n = args_strtonum(args, 'E', INT_MIN, SHRT_MAX, &cause); */
/*     if (cause != NULL) { */
/*       bottom = gd->hsize + gd->sy - 1; */
/*       free(cause); */
/*     } else if (n < 0 && (u_int)-n > gd->hsize) */
/*       bottom = 0; */
/*     else */
/*       bottom = gd->hsize + n; */
/*     if (bottom > gd->hsize + gd->sy - 1) */
/*       bottom = gd->hsize + gd->sy - 1; */
/*   } */
/*  */
/*   if (bottom < top) { */
/*     tmp = bottom; */
/*     bottom = top; */
/*     top = tmp; */
/*   } */
/*  */
/*   with_codes = args_has(args, 'e'); */
/*   escape_c0 = args_has(args, 'C'); */
/*   join_lines = args_has(args, 'J'); */
/*   no_trim = args_has(args, 'N'); */
/*  */
/*   buf = NULL; */
/*   for (i = top; i <= bottom; i++) { */
/*     line = grid_string_cells(gd, 0, i, sx, &gc, with_codes, escape_c0, */
/*                              !join_lines && !no_trim); */
/*     linelen = strlen(line); */
/*  */
/*     buf = cmd_dabbrev_append(buf, len, line, linelen); */
/*  */
/*     gl = grid_peek_line(gd, i); */
/*     if (!join_lines || !(gl->flags & GRID_LINE_WRAPPED)) */
/*       buf[(*len)++] = '\n'; */
/*  */
/*     free(line); */
/*   } */
/*   return (buf); */
/* } */

static enum cmd_retval cmd_dabbrev_exec(struct cmd *self,
                                        struct cmdq_item *item) {
  struct args *args = self->args;
  struct client *c;
  struct window_pane *wp = item->target.wp;
  struct screen *s = &wp->base;
  struct cmd_find_state *fs = &item->target;
  struct grid *gd;
  char *buf, *hint;
  char hint_data[512];
  size_t len;
  /* char *buf, *cause; */
  /* const char *bufname; */
  int with_codes, escape_c0, trim;
  struct grid_cell *gc = NULL;
  int flags = 0;
  struct menu *menu = NULL;
  /* struct menu_item menu_item = {0}; */
  struct menu_item menu_item;
  /* c = cmd_find_client(item, args_get(args, 'c'), 0); */
  c = item->client;

  /* gd = wp->base.grid; */
  /* with_codes = 0; */
  /* escape_c0 = 0; */
  /* trim = 0; */
  /* hint = hint_data + 512 - 1; */
  /*  */
  /* log_debug("%s: %s", __func__, "grab lines"); */
  /* buf = */
  /*     grid_string_cells(gd, 0, s->cy, s->cx, &gc, with_codes, escape_c0,
   * trim); */
  /* len = strlen(buf); */
  /*  */
  /* hint = hint + 1; */
  /* for (int i = len; i >= 0; i--) { */
  /*   if (buf[i] == ' ') { */
  /*     break; */
  /*   } else { */
  /*     hint = hint - 1; */
  /*     *hint = buf[i]; */
  /*   } */
  /* } */
  /* log_debug("%s: word '%s'", __func__, hint); */

  /* menu */
  log_debug("%s: %s", __func__, "start menu");
  menu = menu_create("Completions:");
  menu_item.name = "potto";
  menu_item.key = 'p';
  menu_item.command = "send-keys -l \" monkey \"";
  menu_add_item(menu, &menu_item, NULL, c, fs);
  menu_display(menu, flags, NULL, 1, 1, c, fs, NULL, NULL);
  log_debug("%s: %s", __func__, "end menu");
  /* menu end */

  return (CMD_RETURN_NORMAL);
}

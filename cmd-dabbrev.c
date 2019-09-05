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

static char *cmd_dabbrev_append(char *, size_t *, char *, size_t);
static char *cmd_dabbrev_history(struct args *, struct cmdq_item *,
                                 struct window_pane *, size_t *);

const struct cmd_entry cmd_dabbrev_entry = {
    .name = "dabbrev",
    .alias = "dabbrev",

    .args = {"ab:CeE:JNpPqS:t:", 0, 0},
    .usage = "[-aCeJNpPq] " CMD_BUFFER_USAGE " [-E end-line] "
             "[-S start-line] " CMD_TARGET_PANE_USAGE,

    .target = {'t', CMD_FIND_PANE, 0},

    .flags = CMD_AFTERHOOK,
    .exec = cmd_dabbrev_exec};

static char *cmd_dabbrev_append(char *buf, size_t *len, char *line,
                                size_t linelen) {
  buf = xrealloc(buf, *len + linelen + 1);
  memcpy(buf + *len, line, linelen);
  *len += linelen;
  return (buf);
}

static char *cmd_dabbrev_history(struct args *args, struct cmdq_item *item,
                                 struct window_pane *wp, size_t *len) {
  struct grid *gd;
  const struct grid_line *gl;
  struct grid_cell *gc = NULL;
  int n, with_codes, escape_c0, join_lines, no_trim;
  u_int i, sx, top, bottom, tmp;
  char *cause, *buf, *line;
  const char *Sflag, *Eflag;
  size_t linelen;

  sx = screen_size_x(&wp->base);
  if (args_has(args, 'a')) {
    gd = wp->saved_grid;
    if (gd == NULL) {
      if (!args_has(args, 'q')) {
        cmdq_error(item, "no alternate screen");
        return (NULL);
      }
      return (xstrdup(""));
    }
  } else
    gd = wp->base.grid;

  Sflag = args_get(args, 'S');
  if (Sflag != NULL && strcmp(Sflag, "-") == 0)
    top = 0;
  else {
    n = args_strtonum(args, 'S', INT_MIN, SHRT_MAX, &cause);
    if (cause != NULL) {
      top = gd->hsize;
      free(cause);
    } else if (n < 0 && (u_int)-n > gd->hsize)
      top = 0;
    else
      top = gd->hsize + n;
    if (top > gd->hsize + gd->sy - 1)
      top = gd->hsize + gd->sy - 1;
  }

  Eflag = args_get(args, 'E');
  if (Eflag != NULL && strcmp(Eflag, "-") == 0)
    bottom = gd->hsize + gd->sy - 1;
  else {
    n = args_strtonum(args, 'E', INT_MIN, SHRT_MAX, &cause);
    if (cause != NULL) {
      bottom = gd->hsize + gd->sy - 1;
      free(cause);
    } else if (n < 0 && (u_int)-n > gd->hsize)
      bottom = 0;
    else
      bottom = gd->hsize + n;
    if (bottom > gd->hsize + gd->sy - 1)
      bottom = gd->hsize + gd->sy - 1;
  }

  if (bottom < top) {
    tmp = bottom;
    bottom = top;
    top = tmp;
  }

  with_codes = args_has(args, 'e');
  escape_c0 = args_has(args, 'C');
  join_lines = args_has(args, 'J');
  no_trim = args_has(args, 'N');

  buf = NULL;
  for (i = top; i <= bottom; i++) {
    line = grid_string_cells(gd, 0, i, sx, &gc, with_codes, escape_c0,
                             !join_lines && !no_trim);
    linelen = strlen(line);

    buf = cmd_dabbrev_append(buf, len, line, linelen);

    gl = grid_peek_line(gd, i);
    if (!join_lines || !(gl->flags & GRID_LINE_WRAPPED))
      buf[(*len)++] = '\n';

    free(line);
  }
  return (buf);
}

static enum cmd_retval cmd_dabbrev_exec(struct cmd *self,
                                        struct cmdq_item *item) {
  struct args *args = self->args;
  struct client *c;
  struct window_pane *wp;
  struct winlink *wl = item->target.wl;
  char *buf;
  /* char *buf, *cause; */
  /* const char *bufname; */
  size_t len;

  TAILQ_FOREACH(wp, &wl->window->panes, entry) {
    len = 0;
    buf = cmd_dabbrev_history(args, item, wp, &len);
    if (buf == NULL)
      return (CMD_RETURN_ERROR);

    /* if (args_has(args, 'p')) { */
    c = item->client;
    if (c == NULL || (c->session != NULL && !(c->flags & CLIENT_CONTROL))) {
      cmdq_error(item, "can't write to stdout");
      free(buf);
      return (CMD_RETURN_ERROR);
    }
    evbuffer_add(c->stdout_data, buf, len);
    free(buf);
    if (args_has(args, 'P') && len > 0)
      evbuffer_add(c->stdout_data, "\n", 1);
    server_client_push_stdout(c);
  }
  /* } else { */
  /*   bufname = NULL; */
  /*   if (args_has(args, 'b')) */
  /*     bufname = args_get(args, 'b'); */
  /*  */
  /*   if (paste_set(buf, len, bufname, &cause) != 0) { */
  /*     cmdq_error(item, "%s", cause); */
  /*     free(cause); */
  /*     free(buf); */
  /*     return (CMD_RETURN_ERROR); */
  /*   } */
  /* } */

  return (CMD_RETURN_NORMAL);
}

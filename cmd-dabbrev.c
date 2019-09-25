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

#include "dabbrev-parser.h"
#include "tmux.h"

/*
 * Dynamic Abbreviate, i.e. dabbrev
 */

static char *cmd_dabbrev_gd_buf(char *, struct grid *, u_int, size_t *);
static void display_completions(wchar_t **matches, int num_matches,
                                int hint_len, struct client *c,
                                struct cmd_find_state *fs,
                                struct window_pane *wp);
static int prefix_hint(char **hint, struct window_pane *wp);
static enum cmd_retval cmd_dabbrev_exec(struct cmd *, struct cmdq_item *);

static char *cmd_dabbrev_append(char *, size_t *, char *, size_t);
static char *cmd_dabbrev_window_history(struct window_pane *, size_t *);
struct grid_handle *cmd_dabbrev_open_grid(struct window_pane *);
wint_t cmd_dabbrev_get_next_grid_wchar(struct grid_handle *);

const struct cmd_entry cmd_dabbrev_entry = {
    .name = "dabbrev",
    .alias = "dabbrev",

    .args = {"ab:CeE:JNpPqS:t:", 0, 0},
    .usage = "[-aCeJNpPq] " CMD_BUFFER_USAGE " [-E end-line] "
             "[-S start-line] " CMD_TARGET_PANE_USAGE,

    .target = {'t', CMD_FIND_PANE, 0},

    .flags = CMD_AFTERHOOK,
    .exec = cmd_dabbrev_exec};

static char *cmd_dabbrev_append(char *buf, size_t *buflen, char *line,
                                size_t linelen) {
  buf = xrealloc(buf, *buflen + linelen + 1);
  memcpy(buf + *buflen, line, linelen);
  *buflen += linelen;
  return (buf);
}

static char *cmd_dabbrev_window_history(struct window_pane *wp,
                                        size_t *buflen) {
  u_int sx;
  struct grid *gd;
  char *buf = NULL;

  RB_FOREACH(wp, window_pane_tree, &all_window_panes) {
    sx = screen_size_x(&wp->base);
    gd = wp->base.grid;
    buf = cmd_dabbrev_gd_buf(buf, gd, sx, buflen);
    /* copy alternate screen to buf if available */
    if (wp->saved_grid != NULL) {
      gd = wp->saved_grid;
      buf = cmd_dabbrev_gd_buf(buf, gd, sx, buflen);
    }
  }
  return (buf);
}

struct grid_handle *cmd_dabbrev_open_grid(struct window_pane *wp) {
  struct grid_handle *gh;
  struct grid *gd;

  gd = wp->base.grid;

  gh = xmalloc(sizeof *gh);
  gh->grid = gd;
  gh->gl = grid_peek_line(gd, 0);
  gh->x = 0;
  gh->y = 0;
  gh->curx = 0;
  gh->cury = 0;
  gh->sx = gd->sx;
  gh->sy = gd->sy;

  return (gh);
}

/*
 * 1. check if cell is in line
 *   1a. if it is not get the next line
 *     1i. if line is wrapped get first char of new line
 *     1ii. else return \n
 *   1b. else
 *     1i. if the cell is paded skip to the next cell
 *     1i. else return cell
 *
 */

wint_t cmd_dabbrev_get_next_grid_wchar(struct grid_handle *gh) {

  enum utf8_state utf8s;
  wint_t wc;
  struct grid_cell gc;
  u_int xx, yy;
  FILE *cell_log;
  int found_wchar;
  struct grid *gd;

  gd = gh->grid;

  cell_log = fopen("/tmp/cell_log.out", "a");
  fwprintf(cell_log, L"\nBEGIN GET WCHAR\n");

  found_wchar = 0;
  for (yy = gh->cury; yy < gh->y + gh->sy; yy++) {
    fwprintf(cell_log, L"top of y for\n");

    if (gh->cury != yy) {
      gh->curx = gh->x;
      fwprintf(cell_log, L"Need new line!\n");
      gh->cury = yy;
      if (yy >= gd->hsize + gd->sy) {
        fwprintf(cell_log, L"End of grid WEOF\n");
        found_wchar = 1;
        wc = WEOF;
        break;
      }
      gh->gl = grid_peek_line(gd, yy);
      if (gh->gl->flags & GRID_LINE_WRAPPED) {
        /* grab char from new line */
        fwprintf(cell_log, L"wrapped line\n");
      } else {
        found_wchar = 1;
        wc = L'\n';
        break;
      }
    }

    for (xx = gh->curx; xx < gh->x + gh->sx; xx++) {
      fwprintf(cell_log, L"top of x for\n");
      fflush(cell_log);

      /* XXX Need a new line, put in func? */
      if (gh->gl == NULL || xx >= gh->gl->cellsize) {
        fwprintf(cell_log, L"gl is NULL or xx > cellsize\n");
        break;
      }

      fwprintf(cell_log, L"get cell: %d,%d\n", xx, yy);
      grid_get_cell(gd, xx, yy, &gc);
      if (gc.flags & GRID_FLAG_PADDING) {
        fwprintf(cell_log, L"\tcell is padded\n");
        continue;
      } else if (gc.flags & GRID_FLAG_CLEARED) {
        /* XXX should I get here? */
        fwprintf(cell_log, L"\tcell is cleared\n");
        continue;
      } else {
        utf8s = utf8_combine(&gc.data, &wc);
        if (utf8s == UTF8_ERROR) {
          fwprintf(cell_log, L"\tbad utf8 combine\n");
          wc = L'?';
        }
        found_wchar = 1;
        gh->curx = xx + 1;
        fwprintf(cell_log, L"found wchar break, '%lc'\n", wc);
        break;
      }
      fwprintf(cell_log, L"end of x for\n");
    }

    if (found_wchar) {
      fwprintf(cell_log, L"found wchar break\n");
      break;
    }

    fwprintf(cell_log, L"end of y for\n");
    fflush(cell_log);
  }

  if (!found_wchar) {
    wc = WEOF;
  }
  if (wc == WEOF) {
    fwprintf(cell_log, L"done got wchar_t: 'WEOF'\n");
  } else {
    fwprintf(cell_log, L"done got wchar_t: '%lc'\n", wc);
  }
  fwprintf(cell_log, L"END GET WCHAR\n");
  fclose(cell_log);
  return (wc);
}

static char *cmd_dabbrev_gd_buf(char *buf, struct grid *gd, u_int sx,
                                size_t *buflen) {
  struct grid_cell *gc = NULL;
  const struct grid_line *gl;
  int with_codes, escape_c0, trim;
  u_int i, top, bottom;
  size_t linelen;
  char *line;

  top = 0;
  with_codes = 0;
  escape_c0 = 0;
  trim = 0;

  bottom = gd->hsize + gd->sy - 1;

  for (i = top; i <= bottom; i++) {
    line = grid_string_cells(gd, 0, i, sx, &gc, with_codes, escape_c0, trim);
    linelen = strlen(line);

    buf = cmd_dabbrev_append(buf, buflen, line, linelen);

    gl = grid_peek_line(gd, i);
    if (!(gl->flags & GRID_LINE_WRAPPED)) {
      buf[(*buflen)++] = '\n';
    }

    free(line);
  }

  return buf;
}

static int prefix_hint(char **strp, struct window_pane *wp) {
  int with_codes, escape_c0, trim;
  struct screen *s = &wp->base;
  struct grid *gd;
  char *hint_buf;
  char *hint;
  struct grid_cell *gc = NULL;
  char *buf;
  size_t len;
  int i;
  int max_hint_len = 2048;
  int col_start, col_end, row;

  with_codes = 0;
  escape_c0 = 0;
  trim = 0;
  gd = wp->base.grid;

  log_debug("%s: %s", __func__, "begin");
  col_start = 0;
  row = s->cy;
  col_end = s->cx;
  log_debug("%s: cx: %d", __func__, s->cx);
  log_debug("%s: cy: %d", __func__, s->cy);
  log_debug("%s: row: %d", __func__, row);
  log_debug("%s: col start: %d", __func__, col_start);
  log_debug("%s: col end: %d", __func__, col_end);
  buf = grid_string_cells(gd, col_start, row, col_end, &gc, with_codes,
                          escape_c0, trim);
  log_debug("%s: buf '%s'", __func__, buf);
  len = strlen(buf);

  hint_buf = malloc(max_hint_len * sizeof(char));
  hint = &(hint_buf[max_hint_len - 1]);
  *hint = '\0';

  /* walk string backwards for hint */
  for (i = len; i >= 0; i--) {
    if (buf[i] == ' ') {
      break;
    } else {
      hint--;
      /* hint is too long for str buffer */
      if (hint < hint_buf) {
        return (-1);
      }
      *hint = buf[i];
    }
  }
  log_debug("%s: word '%s'", __func__, hint);
  *strp = xstrdup(hint);
  free(hint_buf);
  log_debug("%s: %s", __func__, "success");
  return (0);
}

/*
 * XXX: support wrapped lines
 */
static int wcprefix_hint(wchar_t **wcs, struct window_pane *wp) {
  struct screen *s = &wp->base;
  struct grid_handle *gh;
  struct grid *gd;

  log_debug("%s: %s", __func__, "begin");

  gd = wp->base.grid;

  gh = xmalloc(sizeof *gh);
  gh->grid = gd;
  gh->gl = grid_peek_line(gd, s->cy);
  gh->x = 0;
  gh->y = s->cy;
  gh->curx = 0;
  gh->cury = s->cy;
  gh->sx = s->cx;
  gh->sy = 1;

  log_debug("%s: cx: %d", __func__, s->cx);
  log_debug("%s: cy: %d", __func__, s->cy);

  last_word(gh, wcs);
  log_debug("%s: done parsing", __func__);

  return (1);
}

static char *wcstr_tombs(wchar_t **wcstr) {
  size_t len;
  char *s;

  len = wcsrtombs(NULL, wcstr, 0, NULL);
  s = xmalloc(len + 1);
  wcsrtombs(s, wcstr, len, NULL);
  return (s);
}

static void display_completions(wchar_t **matches, int num_matches,
                                int hint_len, struct client *c,
                                struct cmd_find_state *fs,
                                struct window_pane *wp) {
  struct screen *s = &wp->base;
  struct menu *menu = NULL;
  struct menu_item menu_item;
  char key;
  int flags = 0;
  int i, m_x, m_y, m_h;
  char *cmd = NULL;
  char *match;

  log_debug("%s: %s", __func__, "start menu");
  menu = menu_create("");
  key = 'a';
  for (i = 0; i < num_matches; i++) {
    match = wcstr_tombs(&(matches[i]));
    log_debug("%s: %s", __func__, "match converted");
    menu_item.name = match;
    menu_item.key = key;
    asprintf(&cmd, "%s%s", "send-keys -l ", match + hint_len);
    menu_item.command = cmd;
    menu_add_item(menu, &menu_item, NULL, c, fs);
    key++;
  }
  m_h = menu->count + 2;
  m_x = wp->xoff + s->cx - hint_len - 2;
  /* If menu is too large to place below the cursor then place it above the
   * cursor */
  if ((wp->yoff + s->cy + m_h) > wp->window->sy) {
    m_y = wp->yoff + s->cy - m_h;
  } else {
    m_y = wp->yoff + s->cy + 1;
  }
  log_debug("%s: %s", __func__, "ready to display menu");
  menu_display(menu, flags, NULL, m_x, m_y, c, fs, NULL, NULL);
  log_debug("%s: %s", __func__, "end menu");
}

static enum cmd_retval cmd_dabbrev_exec(struct cmd *self,
                                        struct cmdq_item *item) {
  struct window_pane *wp = item->target.wp;
  int num_matches = 0;
  wchar_t *hint;
  struct cmd_find_state *fs = &item->target;
  struct client *c;
  wchar_t **matches;

  c = item->client;

  /* 1. grab hint */
  /* if (prefix_hint(&hint, wp) != 0) { */
  /*   return (CMD_RETURN_ERROR); */
  /* } */
  wcprefix_hint(&hint, wp);

  /* 2. grab buffer panes */
  /* histlen = 0; */
  /* buf = cmd_dabbrev_window_history(wp, &histlen); */
  /* if (buf == NULL) { */
  /*   return (CMD_RETURN_ERROR); */
  /* } */

  log_debug("%s: %s", __func__, "complete_hint");
  num_matches = complete_hint(wp, hint, &matches);

  log_debug("%s: %s: %d", __func__, "display matches", num_matches);
  /* 5. display completions */
  display_completions(matches, num_matches, wcslen(hint), c, fs, wp);

  /* 6. cleanup */
  log_debug("%s: %s", __func__, "start cleanup");
  /* free(hint); */
  /* for (i = 0; i < num_words; i++) { */
  /*   free(words[i]); */
  /* } */
  /* free(words); */
  log_debug("%s: %s", __func__, "end cleanup");

  log_debug("%s: %s", __func__, "dabbrev success");
  return (CMD_RETURN_NORMAL);
}

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

static void display_completions(wchar_t const **matches, int num_matches,
                                int hint_len, struct client *c,
                                struct cmd_find_state *fs,
                                struct window_pane *wp);
static enum cmd_retval cmd_dabbrev_exec(struct cmd *, struct cmdq_item *);

struct grid_handle *cmd_dabbrev_open_grid(struct window_pane *);
wint_t cmd_dabbrev_get_next_grid_wchar(struct grid_handle *);
static int grid_get_cell_wchar(struct grid *, u_int, u_int, wint_t *);

const struct cmd_entry cmd_dabbrev_entry = {
    .name = "dabbrev",
    .alias = "dabbrev",

    .args = {"ab:CeE:JNpPqS:t:", 0, 0},
    .usage = "[-aCeJNpPq] " CMD_BUFFER_USAGE " [-E end-line] "
             "[-S start-line] " CMD_TARGET_PANE_USAGE,

    .target = {'t', CMD_FIND_PANE, 0},

    .flags = CMD_AFTERHOOK,
    .exec = cmd_dabbrev_exec};

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
  wint_t wc;
  const struct grid_line *last_gl;
  u_int xx, yy;
  struct grid *gd;

  gd = gh->grid;

  /* Loop over each line in the grid until we find a wchar to return, start
   * where we last left off based off the state in gh */
  for (yy = gh->cury; (yy < gh->y + gh->sy) && (yy < gd->hsize + gd->sy);
       yy++) {

    /* get new line if necessary */
    if (gh->cury != yy) {
      last_gl = gh->gl;
      gh->gl = grid_peek_line(gd, yy);
      gh->cury = yy;
      gh->curx = gh->x;
      /* if the previous line was not wrapped emit a '\n' to finish the
       * previous line */
      if ((last_gl->flags & GRID_LINE_WRAPPED) == 0) {
        return (L'\n');
      }
    }

    /* loop over each cell in the line */
    for (xx = gh->curx; (xx < gh->x + gh->sx) && (xx < gh->gl->cellsize);
         xx++) {
      if (grid_get_cell_wchar(gd, xx, yy, &wc)) {
        gh->curx = xx + 1;
        return (wc);
      }
    }
  }

  /* end of grid rectangle */
  return (WEOF);
}

static int grid_get_cell_wchar(struct grid *gd, u_int x, u_int y, wint_t *wc) {
  struct grid_cell gc;

  grid_get_cell(gd, x, y, &gc);

  if (gc.flags & GRID_FLAG_PADDING) {
    return (0);
  } else if (gc.flags & GRID_FLAG_CLEARED) {
    return (0);
  } else {
    if (utf8_combine(&gc.data, wc) == UTF8_ERROR) {
      *wc = L'?';
    }
    return (1);
  }
}

/*
 * XXX: support wrapped lines, i.e. if previous line was wrapped join it with
 * this line before parsing
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

static char *wcstr_tombs(wchar_t const **wcstr) {
  size_t len;
  char *s;

  len = wcsrtombs(NULL, wcstr, 0, NULL);
  s = xmalloc(len + 1);
  wcsrtombs(s, wcstr, len + 1, NULL);
  return (s);
}

static void display_completions(wchar_t const **matches, int num_matches,
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
    log_debug("%s: %s '%s'", __func__, "match converted", match);
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
  wchar_t const **matches;

  c = item->client;

  /* 1. grab hint */
  /* if (prefix_hint(&hint, wp) != 0) { */
  /*   return (CMD_RETURN_ERROR); */
  /* } */
  wcprefix_hint(&hint, wp);

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

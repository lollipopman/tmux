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
 * Dynamic Abbreviate, i.e. dabbrev
 */

static char *cmd_dabbrev_gd_buf(char *buf, struct grid *gd, u_int sx,
                                size_t *len);
static void display_completions(char **matches, int num_matches, int hint_len,
                                struct client *c, struct cmd_find_state *fs,
                                struct window_pane *wp);
static int prefix_hint(char **hint, struct window_pane *wp);
static char **word_parser(char *buf, int *num_words);
static char **find_matches(char **text, int text_len, char *prefix,
                           int *num_matches);
static enum cmd_retval cmd_dabbrev_exec(struct cmd *, struct cmdq_item *);

static char *cmd_dabbrev_append(char *, size_t *, char *, size_t);
static char *cmd_dabbrev_window_history(struct window_pane *, size_t *);

static char **find_matches(char **text, int text_len, char *prefix,
                           int *num_matches) {
  size_t prefix_len;
  char **matches = NULL;
  int i;

  prefix_len = strlen(prefix);
  for (i = 0; i < text_len; i++) {
    if (strncmp(prefix, text[i], prefix_len) == 0) {
      (*num_matches)++;
      matches = reallocarray(matches, sizeof(char *), *num_matches);
      matches[*num_matches - 1] = strdup(text[i]);
    }
  }

  log_debug("%s: num_matches %d", __func__, *num_matches);
  for (i = 0; i < *num_matches; i++) {
    log_debug("%s: match '%s'", __func__, matches[i]);
  }

  return matches;
}

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

char **word_parser(char *buf, int *num_words) {
  char *cur;
  int i;
  int words_size = 4096;
  char **words;

  words = malloc(words_size * sizeof(char *));
  *num_words = 0;

  /* XXX Is buf guaranteed to be null terminated? */
  /* str[len] = '\0'; */
  cur = buf;
  i = 0;
  while (sscanf(cur, "%ms%n", &(words[*num_words]), &i) == 1) {
    cur += i;
    (*num_words)++;
    if (*num_words > (words_size - 1)) {
      words = reallocarray(words, sizeof(char *), words_size + 4096);
      words_size += 4096;
    }
  }

  return words;
}

static void display_completions(char **matches, int num_matches, int hint_len,
                                struct client *c, struct cmd_find_state *fs,
                                struct window_pane *wp) {
  struct screen *s = &wp->base;
  struct menu *menu = NULL;
  struct menu_item menu_item;
  char key;
  int flags = 0;
  int i, m_x, m_y, m_h;
  char *cmd = NULL;

  log_debug("%s: %s", __func__, "start menu");
  menu = menu_create("");
  key = 'a';
  for (i = 0; i < num_matches; i++) {
    menu_item.name = matches[i];
    menu_item.key = key;
    asprintf(&cmd, "%s%s", "send-keys -l ", matches[i] + hint_len);
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
  menu_display(menu, flags, NULL, m_x, m_y, c, fs, NULL, NULL);
  log_debug("%s: %s", __func__, "end menu");
}

static enum cmd_retval cmd_dabbrev_exec(struct cmd *self,
                                        struct cmdq_item *item) {
  struct window_pane *wp = item->target.wp;
  size_t histlen;
  int i;
  char **words;
  char **matches;
  int num_matches = 0;
  int num_words = 0;
  char *hint;
  char *buf;
  struct cmd_find_state *fs = &item->target;
  struct client *c;
  c = item->client;
  matches = NULL;

  /* 1. grab hint */
  if (prefix_hint(&hint, wp) != 0) {
    return (CMD_RETURN_ERROR);
  }

  /* 2. grab buffer panes */
  histlen = 0;
  buf = cmd_dabbrev_window_history(wp, &histlen);
  if (buf == NULL) {
    return (CMD_RETURN_ERROR);
  }

  /* 3. parse out words */
  buf[histlen] = '\0';
  words = word_parser(buf, &num_words);

  /* 4. find completions */
  matches = find_matches(words, num_words, hint, &num_matches);

  /* 5. display completions */
  display_completions(matches, num_matches, strlen(hint), c, fs, wp);

  /* 6. cleanup */
  log_debug("%s: %s", __func__, "start cleanup");
  free(hint);
  for (i = 0; i < num_words; i++) {
    free(words[i]);
  }
  free(words);
  log_debug("%s: %s", __func__, "end cleanup");

  log_debug("%s: %s", __func__, "dabbrev success");
  return (CMD_RETURN_NORMAL);
}

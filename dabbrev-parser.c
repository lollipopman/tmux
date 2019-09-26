#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tmux.h"
#include "wtst.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static FILE *WCLog;

/* Input parser context. */
struct input_ctx {
  wchar_t word[256];
  struct wchar_node *wtst_root;
  const struct input_state *state;
  wint_t wc;
};

/* Input transition. */
struct input_transition {
  int (*wcclass)(wint_t wc);
  int (*handler)(struct input_ctx *);
  const struct input_state *state;
};

/* Input state. */
struct input_state {
  const wchar_t *name;
  void (*enter)(struct input_ctx *);
  void (*exit)(struct input_ctx *);
  const struct input_transition *transitions;
};

void dabbrev_parser_init(struct input_ctx *);
int last_word(struct grid_handle *, wchar_t **);
size_t complete_hint(struct window_pane *, const wchar_t *, wchar_t const ***);
int parse_grid(struct grid_handle *, struct input_ctx *);
static void input_set_state(struct input_ctx *,
                            const struct input_transition *);

static const struct input_transition input_state_ground_table[];
static const struct input_transition input_state_word_table[];
static const struct input_transition input_state_word_quoted_table[];
static const struct input_transition input_state_uri_maybe_table[];
static const struct input_transition input_state_uri_end_scheme_table[];
static const struct input_transition input_state_uri_post_scheme_table[];
static const struct input_transition input_state_uri_auth_table[];
static const struct input_transition input_state_uri_path_table[];

static void word_continue(struct input_ctx *);
static void word_begin(struct input_ctx *);
static void uri_auth_begin(struct input_ctx *);
static int word_collect(struct input_ctx *);
static int word_print(struct input_ctx *);

static int iswcolon(wint_t wc);
static int iswurischeme(wint_t wc);
static int iswquote(wint_t wc);
static int iswpipe(wint_t wc);
static int iswforwardslash(wint_t wc);

/* Reset for ground state. */
static void input_ground(struct input_ctx *ictx) { ictx->word[0] = L'\0'; }

/* ground state definition. */
static const struct input_state input_state_ground = {
    L"ground", input_ground, NULL, input_state_ground_table};

/* word state definition. */
static const struct input_state input_state_word = {
    L"word", word_continue, NULL, input_state_word_table};

/* word_quoted state definition. */
static const struct input_state input_state_word_quoted = {
    L"word_quoted", word_begin, NULL, input_state_word_quoted_table};

/* uri_maybe state definition. */
static const struct input_state input_state_uri_maybe = {
    L"uri_maybe", word_continue, NULL, input_state_uri_maybe_table};

/* uri_end_scheme state definition. */
static const struct input_state input_state_uri_end_scheme = {
    L"uri_end_scheme", word_continue, NULL, input_state_uri_end_scheme_table};

/* uri_auth state definition. */
static const struct input_state input_state_uri_auth = {
    L"uri_auth", uri_auth_begin, NULL, input_state_uri_auth_table};

/* uri_post_scheme state definition. */
static const struct input_state input_state_uri_post_scheme = {
    L"uri_post_scheme", word_continue, NULL, input_state_uri_post_scheme_table};

/* uri_path state definition. */
static const struct input_state input_state_uri_path = {
    L"uri_path", word_begin, NULL, input_state_uri_path_table};

/* BEGIN STATE TABLES */

/* ground state table. */
static const struct input_transition input_state_ground_table[] = {
    {iswspace, NULL, NULL},
    {iswquote, NULL, &input_state_word_quoted},
    {iswalpha, NULL, &input_state_uri_maybe},
    {iswprint, NULL, &input_state_word},
    {NULL, NULL, NULL}};

/* uri_maybe state table. */
static const struct input_transition input_state_uri_maybe_table[] = {
    {iswcolon, word_print, &input_state_uri_end_scheme},
    {iswurischeme, word_collect, NULL},
    {iswcolon, word_print, &input_state_ground},
    {iswpipe, word_print, &input_state_ground},
    {iswspace, word_print, &input_state_ground},
    {iswgraph, NULL, &input_state_word},
    {NULL, NULL, NULL}};

/* uri_path state table. */
static const struct input_transition input_state_uri_path_table[] = {
    {iswgraph, word_collect, NULL},
    {iswspace, word_print, &input_state_ground},
    {NULL, NULL, NULL}};

/* uri_end_scheme state table. */
static const struct input_transition input_state_uri_end_scheme_table[] = {
    {iswforwardslash, NULL, &input_state_uri_post_scheme},
    {iswgraph, NULL, &input_state_uri_path},
    {iswspace, NULL, &input_state_ground},
    {NULL, NULL, NULL}};

/* uri_post_scheme state table. */
static const struct input_transition input_state_uri_post_scheme_table[] = {
    {iswforwardslash, NULL, &input_state_uri_auth},
    {iswgraph, NULL, &input_state_uri_path},
    {iswspace, word_print, &input_state_ground},
    {NULL, NULL, NULL}};

/* uri_auth state table. */
static const struct input_transition input_state_uri_auth_table[] = {
    {iswforwardslash, word_print, &input_state_uri_path},
    {iswgraph, word_collect, NULL},
    {iswspace, word_print, &input_state_ground},
    {NULL, NULL, NULL}};

/* word state table. */
static const struct input_transition input_state_word_table[] = {
    {iswcolon, word_print, &input_state_ground},
    {iswpipe, word_print, &input_state_ground},
    {iswspace, word_print, &input_state_ground},
    {iswgraph, word_collect, NULL},
    {NULL, NULL, NULL}};

/* word quoted table. */
static const struct input_transition input_state_word_quoted_table[] = {
    {iswquote, NULL, &input_state_word},
    {iswprint, word_collect, NULL},
    {iswcntrl, NULL, &input_state_ground},
    {NULL, NULL, NULL}};

static int iswcolon(wint_t wc) { return (wc == L':'); }
static int iswurischeme(wint_t wc) {
  if (iswalnum(wc)) {
    return (1);
  } else if (wc == L'+') {
    return (1);
  } else if (wc == L'-') {
    return (1);
  } else if (wc == L'.') {
    return (1);
  } else {
    return (0);
  }
}

static int iswquote(wint_t wc) {
  if (wc == L'\'' || wc == L'"') {
    return (1);
  } else {
    return (0);
  }
}

static int iswpipe(wint_t wc) { return (wc == L'|'); }

static int iswforwardslash(wint_t wc) { return (wc == L'/'); }

static int word_collect(struct input_ctx *ictx) {
  int len;

  len = wcslen(ictx->word);
  ictx->word[len] = ictx->wc;
  ictx->word[len + 1] = '\0';
  return (0);
}

static void word_continue(struct input_ctx *ictx) {
  int len;

  len = wcslen(ictx->word);
  ictx->word[len] = ictx->wc;
  ictx->word[len + 1] = '\0';
}

static void word_begin(struct input_ctx *ictx) {
  ictx->word[0] = ictx->wc;
  ictx->word[1] = '\0';
}

static void uri_auth_begin(struct input_ctx *ictx) { ictx->word[0] = '\0'; }

static int word_print(struct input_ctx *ictx) {
  fwprintf(WCLog, L"Word: |%ls|\n", ictx->word);
  word_add(&(ictx->wtst_root), ictx->word);
  return (0);
}

/* Change input state. */
static void input_set_state(struct input_ctx *ictx,
                            const struct input_transition *itr) {
  fwprintf(WCLog, L"Change state, current word: '%ls'\n", ictx->word);
  if (ictx->state->exit != NULL)
    ictx->state->exit(ictx);
  fwprintf(WCLog, L"\t%ls --'", ictx->state->name);
  fwprintf(WCLog, L"%lc", ictx->wc);
  ictx->state = itr->state;
  fwprintf(WCLog, L"'--> %ls\n", ictx->state->name);
  if (ictx->state->enter != NULL)
    ictx->state->enter(ictx);
}

size_t complete_hint(struct window_pane *wp, const wchar_t *hint,
                     wchar_t const ***word_list) {
  struct input_ctx *ictx;
  struct grid_handle *gh;

  gh = cmd_dabbrev_open_grid(wp);
  ictx = xcalloc(1, sizeof *ictx);
  dabbrev_parser_init(ictx);
  parse_grid(gh, ictx);
  return (word_gather(ictx->wtst_root, hint, word_list));
}

int last_word(struct grid_handle *gh, wchar_t **word) {
  struct input_ctx *ictx;
  static FILE *LWLog;

  LWLog = fopen("/tmp/last_word.out", "w");
  fwprintf(LWLog, L"begin\n");
  ictx = xcalloc(1, sizeof *ictx);
  dabbrev_parser_init(ictx);
  fwprintf(LWLog, L"parsing\n");
  fclose(LWLog);

  parse_grid(gh, ictx);

  LWLog = fopen("/tmp/last_word.out", "a");
  fwprintf(LWLog, L"done parsing\n");
  *word = wcsdup(ictx->word);
  fwprintf(LWLog, L"word: '%ls'\n", *word);
  fclose(LWLog);
  return 1;
}

void dabbrev_parser_init(struct input_ctx *ictx) {

  ictx->state = &input_state_ground;
  ictx->wtst_root = NULL;
  input_ground(ictx);
}

int parse_grid(struct grid_handle *gh, struct input_ctx *ictx) {
  const struct input_transition *itr;

  WCLog = fopen("/tmp/parser.out", "a");
  fwprintf(WCLog, L"\nStart Parsing\n");

  setlocale(LC_CTYPE, "en_US.UTF-8");

  /* Parse the input. */
  for (;;) {
    ictx->wc = cmd_dabbrev_get_next_grid_wchar(gh);
    if (ictx->wc == WEOF) {
      fwprintf(WCLog, L"WEOF\n");
      break;
    }

    /* Find the transition. */
    itr = ictx->state->transitions;
    while (itr->wcclass != NULL) {
      if (itr->wcclass(ictx->wc)) {
        break;
      }
      itr++;
    }
    if (itr->wcclass == NULL) {
      fwprintf(WCLog, L"No transition from state:\n");
      fwprintf(WCLog, L"\tchar: '%lc'\n", ictx->wc);
      fwprintf(WCLog, L"\tstate: '%ls'\n", ictx->state->name);
      return (-1);
    }

    /*
     * Execute the handler, if any. Don't switch state if it
     * returns non-zero.
     */
    if (itr->handler != NULL && itr->handler(ictx) != 0) {
      continue;
    }

    /* And switch state, if necessary. */
    if (itr->state != NULL)
      input_set_state(ictx, itr);
  }

  fclose(WCLog);
  return (1);
}

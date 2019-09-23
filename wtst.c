#define _GNU_SOURCE
#include <limits.h>
#include <locale.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

struct wchar_node {
  wchar_t wc;
  int word_end;

  struct wchar_node *left;
  struct wchar_node *right;

  struct wchar_node *next;
};

void word_add(struct wchar_node **, const wchar_t *);
static void word_free1(struct wchar_node *);
static wchar_t *word_find1(struct wchar_node *, const wchar_t *, size_t,
                           size_t *);
static wchar_t *word_find(struct wchar_node *, const wchar_t *, size_t,
                          size_t *);
static struct wchar_node *word_prefix(struct wchar_node *, const wchar_t *);
static struct wchar_node *word_prefix1(struct wchar_node *, const wchar_t *,
                                       size_t);
size_t word_gather(struct wchar_node *, const wchar_t *, wchar_t ***);
static void word_gather1(struct wchar_node *, const wchar_t *, wchar_t ***,
                         size_t *);

/* Add next node to the tree. */
void word_add(struct wchar_node **wnp, const wchar_t *s) {
  struct wchar_node *wn;

  /* Allocate a tree entry if there isn't one already. */
  wn = *wnp;
  if (wn == NULL) {
    wn = *wnp = calloc(1, sizeof *wn);
    wn->wc = *s;
    wn->word_end = 0;
  }

  /* Find the next entry. */
  if (*s == wn->wc) {
    /* Move forward in string. */
    s++;

    /* If this is the end of the string, no more is necessary. */
    if (*s == '\0') {
      wn->word_end = 1;
      return;
    }

    /* Use the child tree for the next character. */
    wnp = &wn->next;
  } else {
    if (*s < wn->wc)
      wnp = &wn->left;
    else if (*s > wn->wc)
      wnp = &wn->right;
  }

  /* And recurse to add it. */
  word_add(wnp, s);
}

/* Lookup a word in the tree. */
static wchar_t *word_find(struct wchar_node *word_tree, const wchar_t *buf,
                          size_t len, size_t *size) {
  *size = 0;
  return (word_find1(word_tree, buf, len, size));
}

/* Find the next node. */
static wchar_t *word_find1(struct wchar_node *wn, const wchar_t *buf,
                           size_t len, size_t *size) {
  wchar_t *word, *s_prefix;
  int prefix_len;

  /* If no data, no match. */
  if (len == 0)
    return (NULL);

  /* If the node is NULL, this is the end of the tree. No match. */
  if (wn == NULL)
    return (NULL);

  /* Pick the next in the sequence. */
  if (wn->wc == *buf) {
    /* Move forward in the string. */
    buf++;
    len--;
    (*size)++;

    /* At the end of the string, return the current node. */
    if (len == 0) {
      wprintf(L"At the end of the string, return the current node.\n");
      if (wn->word_end) {
        word = (wchar_t *)malloc(2 * sizeof(wchar_t));
        word[0] = wn->wc;
        word[1] = '\0';
        return (word);
      } else {
        return (NULL);
      }
    }

    /* Move into the next tree for the following character. */
    s_prefix = word_find1(wn->next, buf, len, size);
    if (s_prefix == NULL) {
      return (NULL);
    } else {
      prefix_len = wcslen(s_prefix);
      word = (wchar_t *)malloc((2 + prefix_len) * sizeof(wchar_t));
      word[0] = wn->wc;
      wcscpy(word + 1, s_prefix);
      word[prefix_len + 1] = '\0';
      return (word);
    }
  } else {
    if (*buf < wn->wc)
      wn = wn->left;
    else if (*buf > wn->wc)
      wn = wn->right;
    /* Move to the next in the tree. */
    return (word_find1(wn, buf, len, size));
  }
}

static struct wchar_node *word_prefix(struct wchar_node *wn, const wchar_t *s) {
  return word_prefix1(wn, s, wcslen(s));
}

static struct wchar_node *word_prefix1(struct wchar_node *wn,
                                       const wchar_t *buf, size_t len) {
  /* If no data, no match. */
  if (len == 0)
    return (NULL);

  /* If the node is NULL, this is the end of the tree. No match. */
  if (wn == NULL)
    return (NULL);

  /* Pick the next in the sequence. */
  if (wn->wc == *buf) {
    /* Move forward in the string. */
    buf++;
    len--;

    /* At the end of the string, return the next node. */
    if (len == 0) {
      return (wn->next);
    }

    wn = wn->next;
  } else {
    if (*buf < wn->wc)
      wn = wn->left;
    else if (*buf > wn->wc)
      wn = wn->right;
  }
  return (word_prefix1(wn, buf, len));
}

static void word_print(struct wchar_node *wn, const wchar_t *s_prefix) {
  /* If the node is NULL, this is the end of the tree. No match. */
  int prefix_len;
  wchar_t *word;

  if (wn == NULL) {
    return;
  }

  if (wn->word_end) {
    wprintf(L"word: %ls%lc\n", s_prefix, wn->wc);
  }
  prefix_len = wcslen(s_prefix);
  word = (wchar_t *)malloc((2 + prefix_len) * sizeof(wchar_t));
  wcscpy(word, s_prefix);
  word[prefix_len] = wn->wc;
  word[prefix_len + 1] = '\0';

  /* Below recursive order ensures words are sorted lexicographically */
  word_print(wn->left, s_prefix);
  word_print(wn->next, word);
  word_print(wn->right, s_prefix);
}

size_t word_gather(struct wchar_node *wn_root, const wchar_t *s_prefix,
                   wchar_t ***word_listp) {
  size_t num_words;

  *word_listp = NULL;
  num_words = 0;
  word_gather1(word_prefix(wn_root, s_prefix), s_prefix, word_listp,
               &num_words);
  return num_words;
}

static void word_gather1(struct wchar_node *wn, const wchar_t *s_prefix,
                         wchar_t ***word_listp, size_t *num_wordsp) {
  int prefix_len;
  wchar_t *word;
  size_t i;

  /* If the node is NULL, this is the end of the tree. No match. */
  if (wn == NULL) {
    return;
  }

  prefix_len = wcslen(s_prefix);
  word = (wchar_t *)malloc((2 + prefix_len) * sizeof(wchar_t));
  wcscpy(word, s_prefix);
  word[prefix_len] = wn->wc;
  word[prefix_len + 1] = '\0';

  if (wn->word_end) {
    (*num_wordsp)++;
    *word_listp = reallocarray(*word_listp, sizeof(wchar_t *), *num_wordsp);
    (*word_listp)[*num_wordsp - 1] = word;
  }

  /* Below recursive order ensures words are sorted lexicographically */
  word_gather1(wn->left, s_prefix, word_listp, num_wordsp);
  word_gather1(wn->next, word, word_listp, num_wordsp);
  word_gather1(wn->right, s_prefix, word_listp, num_wordsp);
}

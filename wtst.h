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
size_t word_gather(struct wchar_node *, const wchar_t *, wchar_t ***);

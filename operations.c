#include <assert.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operations.h"
#include "status.h"
#include "read.h"

static int expand_replace(
    char *replace_expanded,
    const char *pattern_space,
    const char *replace,
    const regmatch_t *pmatch) {
  const int replace_len = strlen(replace);
  bool found_backslash = false;
  int replace_expanded_index = 0;
  for (int replace_index = 0; replace_index < replace_len; ++replace_index) {
    const char replace_char = replace[replace_index];
    switch (replace_char) {
      case '\\':
        // double backslash case
        if (found_backslash) {
          replace_expanded[replace_expanded_index++] = '\\';
        }
        found_backslash = !found_backslash;
        break;
      case '&':
        if (!found_backslash) {
          const int so = pmatch[0].rm_so;
          const int eo = pmatch[0].rm_eo;
          memmove(
            replace_expanded + replace_expanded_index,
            pattern_space + so,
            eo
          );
          replace_expanded_index += eo - so;
        } else {
          replace_expanded[replace_expanded_index++] = replace_char;
          found_backslash = 0;
        }
        break;
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        if (found_backslash) {
          const char back_ref_index = replace_char - '0';
          const int so = pmatch[back_ref_index].rm_so;
          if (so == -1) {
            return -1;
          }
          const int eo = pmatch[back_ref_index].rm_eo;
          memmove(
            replace_expanded + replace_expanded_index,
            pattern_space + so,
            eo
          );
          replace_expanded_index += eo - so;
          found_backslash = 0;
        } else {
          replace_expanded[replace_expanded_index++] = replace_char;
        }
        break;
      default:
        replace_expanded[replace_expanded_index++] = replace_char;
        if (found_backslash) {
          // ignore for now, at some point it might be nice to handle \x, \o, \n
          // etc.
          found_backslash = 0;
        }
        break;
    }
  }
  return replace_expanded_index;
}

static int substitution(
    regex_t *regex,
    Status *status,
    char *pattern_space,
    const char *pattern,
    const char *replace,
    bool first_sub
) {
  regmatch_t pmatch[MAX_MATCHES];
  if (regexec(
        regex,
        pattern_space,
        MAX_MATCHES,
        pmatch,
        first_sub ? 0 : REG_NOTBOL
  )) {
    regfree(regex);
    return 0;
  }

  status->sub_success = true;

  const int pattern_space_len = strlen(pattern_space);
  const int so = pmatch[0].rm_so; // start offset
  assert(so != -1);
  const int eo = pmatch[0].rm_eo; // end offset
  char replace_expanded[512]; // TODO abitrary size, might be too small
  const int replace_expanded_len =
    expand_replace(replace_expanded, pattern_space, replace, pmatch);

  // empty match, s/^/foo/ for instance
  if (eo == 0) {
    if (first_sub) {
      memmove(
        pattern_space + replace_expanded_len,
        pattern_space,
        pattern_space_len + 1 // include \0
      );
      memmove(pattern_space, replace_expanded, replace_expanded_len);
      return replace_expanded_len;
    } else if (pattern_space_len == 1) {
      // case:  echo 'Hello ' | sed 's|[^ ]*|yo|g'
      pattern_space++;
      memmove(pattern_space, replace_expanded, replace_expanded_len);
      pattern_space[replace_expanded_len] = '\0';
      return replace_expanded_len + 1; // +1 since we did pattern_space++
    }
    return 1;
  }

  int po = 0;
  int ro = 0;

  for (po = so; po < eo && ro < replace_expanded_len; ++po, ++ro) {
    pattern_space[po] = replace_expanded[ro];
  }

  if (po < eo) {
    // Matched part was longer than replaced part, let's shift the rest to the
    // left.
    memmove(
      pattern_space + po,
      pattern_space + eo,
      pattern_space_len - po
    );
    return po;
  } else if (ro < replace_expanded_len) {
    memmove(
      pattern_space + eo + replace_expanded_len - ro,
      pattern_space + eo,
      pattern_space_len - eo
    );
    memmove(
      pattern_space + eo,
      replace_expanded + ro,
      replace_expanded_len - ro
    );

    pattern_space[pattern_space_len + replace_expanded_len - (eo - so)] = 0;
    return so + replace_expanded_len;
  }
  return eo;
}

void s(
  Status *status,
  const char *pattern,
  const char *replace,
  const int opts)
{
  regex_t regex;

  // FIXME we should compile only once, both loops and each line processed can
  // lead to compiling the same regex many times
  if (regcomp(&regex, pattern, 0)) {
    regfree(&regex);
    assert(false);
  }

  // TODO nth/p/w opts
  const bool opt_g = opts & S_OPT_G;

  char *pattern_space = status->pattern_space;
  int pattern_offset = 0;
  bool first_sub = true;
  do {
    pattern_offset = substitution(
      &regex,
      status,
      pattern_space,
      pattern,
      replace,
      first_sub
    );
    // if opt_g is enabled then we want to avoid ^ to keep its meaning for the
    // next iterations
    first_sub = false;
    pattern_space += pattern_offset;
  } while (opt_g && pattern_space[0] && pattern_offset);
  regfree(&regex);
}

void x(Status *status) {
  char *pattern_space = status->pattern_space;
  char *hold_space = status->hold_space;
  status->pattern_space = hold_space;
  status->hold_space = pattern_space;
}

void d(Status *status) {
  status->pattern_space[0] = '\0';
}

operation_ret D(Status *status) {
  char *pattern_space = status->pattern_space;
  const char *newline_location = strchr(pattern_space, '\n');
  if (newline_location == NULL) {
    pattern_space[0] = '\0';
    return CONTINUE;
  }

  memmove(
    pattern_space,
    newline_location + 1, // + 1 to start copying after the newline
    strlen(newline_location + 1) + 1 // last +1 to move \0 as well
  );
  status->skip_read = true;
  return CONTINUE;
}

void equal(Status *status) {
  unsigned int line_nb = status->line_nb;
  printf(
      "%d\n",
      line_nb
  );
}

void g(Status *status) {
  char *pattern_space = status->pattern_space;
  const char *hold_space = status->hold_space;
  memcpy(
    pattern_space,
    hold_space,
    strlen(hold_space) + 1 // include \0
  );
}

void G(Status *status) {
  char *pattern_space = status->pattern_space;
  const char *hold_space = status->hold_space;
  const int pattern_space_len = strlen(pattern_space);
  memcpy(
    pattern_space + pattern_space_len + 1, // we'll place the \n in between
    hold_space,
    strlen(hold_space) + 1 // include \0
  );
  pattern_space[pattern_space_len] = '\n';
}

void h(Status *status) {
  const char *pattern_space = status->pattern_space;
  char *hold_space = status->hold_space;
  memcpy(
    hold_space,
    pattern_space,
    strlen(pattern_space) + 1 // include \0
  );
}

void H(Status *status) {
  const char *pattern_space = status->pattern_space;
  char *hold_space = status->hold_space;
  const int hold_space_len = strlen(hold_space);
  memcpy(
    hold_space + hold_space_len + 1, // we'll place the \n in between
    pattern_space,
    strlen(pattern_space) + 1 // include \0
  );
  hold_space[hold_space_len] = '\n';
}

operation_ret n(Status *status) {
  puts(status->pattern_space);
  if (!read_pattern(status, status->pattern_space, PATTERN_SIZE)) {
    return BREAK;
  }
  return 0;
}

operation_ret N(Status *status) {
  char *pattern_space = status->pattern_space;
  const int pattern_space_len = strlen(pattern_space);
  if (!read_pattern(
        status,
        pattern_space + pattern_space_len + 1,
        PATTERN_SIZE - pattern_space_len - 1)
  ) {
    puts(status->pattern_space);
    return BREAK;
  }
  pattern_space[pattern_space_len] = '\n';
  return 0;
}

void p(const Status *status) {
  const char *pattern_space = status->pattern_space;
  puts(pattern_space);
}

void P(const Status *status) {
  const char *pattern_space = status->pattern_space;
  printf(
    "%.*s\n",
    strchr(pattern_space, '\n') - pattern_space,
    pattern_space
  );
}

void q(const Status *status) {
  p(status);
  exit(0);
}

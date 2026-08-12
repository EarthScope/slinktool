/***************************************************************************
 * Portable glob matcher.  Tests matching of strings against glob patterns.
 *
 * The matcher function's final symbol name can be prefixed by defining
 * `GLOBMATCH_PREFIX` either in globmatch.h or at compile-time.
 *
 * Inspired by Ozan Yigit's 1994 version, posted to Usenet and placed in the
 * public domain, and helping many projects over the years.
 *
 * This code is released into the public domain.
 *
 * Version: 2
 ***************************************************************************/

#include <stddef.h>
#include <string.h>

#include "globmatch.h"

static int _match_charclass (const char **pp, unsigned char c);

/** ************************************************************************
 * @brief Check if a string matches a globbing pattern.
 *
 * Supported semantics:
 * `*` matches zero or more characters, e.g. `*.txt`
 * `?` matches a single character, e.g. `a?c`
 * `[]` matches a set of characters `[abc]`
 * `[a-z]` matches a range of characters `[A-Z]`
 * `[!abc]` negation, matches when no characters in the set, e.g. `[!ABC]` or `[^ABC]`
 * `[!a-z]` negation, matches when no characters in the range, e.g. `[!A-Z]` or `[^A-Z]`
 * `\` prefix to match a literal character, e.g. `\*`, `\?`, `\[`
 *
 * Notes / limitations:
 * - Escapes are not interpreted inside `[...]`; e.g. `[\]]` is a class
 *   containing `\` terminated by the first `]`.
 * - Descending ranges (e.g. `[z-a]`) are treated as the three literal
 *   characters rather than an error.
 * - A trailing `\` with no following character matches a literal `\`.
 *
 * @param string  The string to check.
 * @param pattern The globbing pattern to match.
 *
 * @returns 0 if string does not match pattern and non-zero otherwise.
 ***************************************************************************/
int
GLOBMATCH (globmatch) (const char *string, const char *pattern)
{
  const char *star_p = NULL;   /* position of the most recent '*' in pattern */
  const char *star_s = NULL;   /* position in string when that '*' was seen */
  unsigned char star_skip = 0; /* byte to skip past on backtrack, or 0 if none */
  unsigned char c;

  if (string == NULL || pattern == NULL)
    return 0;

  for (;;)
  {
    c = (unsigned char)*pattern++;

    switch (c)
    {
    case '\0':
      /* End of pattern: must also be end of string unless a previous '*'
         can consume more characters. */
      if (*string == '\0')
        return 1;
      if (star_p)
        goto star_backtrack;
      return 0;

    case '?':
      if (*string == '\0')
        goto star_backtrack;
      string++;
      break;

    case '*':
      /* Collapse consecutive '*' */
      while (*pattern == '*')
        pattern++;

      /* Trailing '*' matches everything */
      if (*pattern == '\0')
        return 1;

      /* Determine the literal byte (if any) following the '*'. If it is a
         literal, we can skip string characters that cannot match it. */
      {
        unsigned char next = (unsigned char)*pattern;

        if (next == '\\' && pattern[1])
          next = (unsigned char)pattern[1];
        else if (next == '?' || next == '[')
          next = 0; /* not a literal; skip the optimization */

        star_skip = next;

        if (star_skip)
        {
          const char *found = strchr (string, star_skip);
          if (found == NULL)
            return 0; /* required literal cannot occur in remaining string */
          string = found;
        }
      }

      star_p = pattern - 1;
      star_s = string;
      continue;

    case '[':
    {
      const char *pp = pattern;
      if (*string == '\0')
        goto star_backtrack;
      if (!_match_charclass (&pp, (unsigned char)*string))
        goto star_backtrack;
      pattern = pp;
      string++;
      break;
    }

    case '\\':
      if (*pattern)
        c = (unsigned char)*pattern++;
      /* FALLTHROUGH */

    default:
      if ((unsigned char)*string != c)
        goto star_backtrack;
      string++;
      break;
    }

    continue;

  star_backtrack:
    /* If there was a previous '*', backtrack: let it consume one more
       character and retry from pattern just after that '*'. */
    if (star_p)
    {
      if (*star_s == '\0')
        return 0;

      star_s++;

      /* Reuse the saved fast-forward byte so we don't walk non-matching
         characters one at a time on each retry. */
      if (star_skip)
      {
        const char *found = strchr (star_s, star_skip);
        if (found == NULL)
          return 0;
        star_s = found;
      }
      else if (*star_s == '\0')
      {
        return 0;
      }

      string = star_s;
      pattern = star_p + 1;
      continue;
    }
    return 0;
  }
}

/***************************************************************************
 * Character class parser helper function.
 *
 *   On entry: *pp points just past '['.
 *             If the class is negated, the next character will be '^'
 *             and is handled inside this function.
 *
 *   On return: *pp is advanced past the closing ']'.
 *
 * Return 1 if c matches the class, 0 otherwise.
 ***************************************************************************/
static int
_match_charclass (const char **pp, unsigned char c)
{
  const char *p;
  int negate = 0;
  int matched = 0;

  if (pp == NULL || *pp == NULL)
    return 0;

  p = *pp;

  /* Handle negation */
  if (*p == '^' || *p == '!')
  {
    negate = 1;
    p++;
  }

  /* Per glob rules, leading ']' is literal */
  if (*p == ']')
  {
    matched = (c == ']');
    p++;
  }

  /* Per glob rules, leading '-' is literal */
  if (*p == '-')
  {
    matched |= (c == '-');
    p++;
  }

  /* Main loop until ']' or end of string */
  while (*p && *p != ']')
  {
    unsigned char pc = (unsigned char)*p;

    if (p[1] == '-' && p[2] && p[2] != ']' && (unsigned char)pc <= (unsigned char)p[2])
    {
      /* Range X-Y (only ascending ranges are supported) */
      unsigned char start = pc;
      unsigned char end = (unsigned char)p[2];

      matched |= (c >= start && c <= end);

      p += 3; /* skip X-Y */
    }
    else
    {
      /* Literal character */
      matched |= (c == pc);
      p++;
    }
  }

  /* Malformed class (no closing ']') → no match */
  if (*p != ']')
  {
    *pp = p;
    return 0;
  }

  *pp = p + 1; /* skip ']' */

  return negate ? !matched : matched;
}

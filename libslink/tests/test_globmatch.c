/***************************************************************************
 * test_globmatch.c: sl_globmatch() coverage.
 *
 * sl_globmatch() was recently rewritten; these pin the documented
 * semantics (literals, '*', '?', character classes, ranges, negation,
 * escaping) and the backtracking edge cases called out in its header
 * comment.
 ***************************************************************************/

#include "libslink.h"
#include "globmatch.h" /* declares sl_globmatch(), not part of the public API */
#include "slt.h"

static void
test_literals (void)
{
  SLT_ASSERT (sl_globmatch ("abc", "abc"), "exact literal match");
  SLT_ASSERT (!sl_globmatch ("abc", "abd"), "literal mismatch rejected");
  SLT_ASSERT (!sl_globmatch ("abc", "ab"), "shorter pattern rejected");
  SLT_ASSERT (!sl_globmatch ("ab", "abc"), "longer pattern rejected");
  SLT_ASSERT (sl_globmatch ("", ""), "empty string matches empty pattern");
  SLT_ASSERT (!sl_globmatch ("a", ""), "non-empty string rejected by empty pattern");
  SLT_ASSERT (sl_globmatch ("", "*"), "empty string matches bare star");
}

static void
test_question (void)
{
  SLT_ASSERT (sl_globmatch ("abc", "a?c"), "single ? matches one character");
  SLT_ASSERT (!sl_globmatch ("ac", "a?c"), "? requires a character to consume");
  SLT_ASSERT (sl_globmatch ("abc", "???"), "??? matches any 3-char string");
  SLT_ASSERT (!sl_globmatch ("ab", "???"), "??? rejects a 2-char string");
}

static void
test_star (void)
{
  SLT_ASSERT (sl_globmatch ("anything.txt", "*.txt"), "leading star matches prefix");
  SLT_ASSERT (sl_globmatch ("x", "*"), "bare star matches everything");
  SLT_ASSERT (sl_globmatch ("", "*"), "bare star matches empty string");
  SLT_ASSERT (sl_globmatch ("aXbXc", "a*c"), "star spans multiple characters");
  SLT_ASSERT (sl_globmatch ("ac", "a*c"), "star can match zero characters");
  SLT_ASSERT (sl_globmatch ("aaaa", "a*a*a"), "collapsed consecutive stars backtrack correctly");
  SLT_ASSERT (!sl_globmatch ("aaab", "a*a*a"), "trailing literal after stars must still match");
  SLT_ASSERT (sl_globmatch ("aXaXaXb", "a*b"), "star backtracks past false starts to find the tail");
  SLT_ASSERT (sl_globmatch ("XX_TEST", "XX_*"), "typical station-id prefix glob");
  SLT_ASSERT (sl_globmatch ("BHZ", "**"), "collapsed multiple stars still match everything");
}

static void
test_charclass (void)
{
  SLT_ASSERT (sl_globmatch ("b", "[abc]"), "class member matches");
  SLT_ASSERT (!sl_globmatch ("d", "[abc]"), "non-member rejected");
  SLT_ASSERT (sl_globmatch ("M", "[A-Z]"), "ascending range matches");
  SLT_ASSERT (!sl_globmatch ("m", "[A-Z]"), "range excludes wrong case");
  SLT_ASSERT (sl_globmatch ("d", "[!abc]"), "'!' negation matches non-member");
  SLT_ASSERT (!sl_globmatch ("a", "[!abc]"), "'!' negation rejects member");
  SLT_ASSERT (sl_globmatch ("d", "[^abc]"), "'^' negation matches non-member");
  SLT_ASSERT (!sl_globmatch ("a", "[^abc]"), "'^' negation rejects member");
  SLT_ASSERT (sl_globmatch ("]", "[]a]"), "leading ']' in a class is a literal");
  SLT_ASSERT (sl_globmatch ("a", "[]a]"), "class with a literal leading ']' still matches other members");
  SLT_ASSERT (sl_globmatch ("-", "[-az]"), "leading '-' in a class is a literal");
  SLT_ASSERT (sl_globmatch ("z", "[-az]"), "class with a literal leading '-' still matches other members");
  SLT_ASSERT (!sl_globmatch ("m", "[-az]"), "class with literal '-' does not form an unintended range");
  SLT_ASSERT (sl_globmatch ("BHZ", "[BLH]HZ"), "class combined with literals");
}

static void
test_escape (void)
{
  SLT_ASSERT (sl_globmatch ("*", "\\*"), "escaped star matches literal star");
  SLT_ASSERT (!sl_globmatch ("x", "\\*"), "escaped star does not match other characters");
  SLT_ASSERT (sl_globmatch ("?", "\\?"), "escaped question mark matches literal");
  SLT_ASSERT (sl_globmatch ("[", "\\["), "escaped bracket matches literal");
  SLT_ASSERT (sl_globmatch ("*.txt", "\\**.txt"), "escaped star followed by a real star");
}

static void
test_malformed (void)
{
  SLT_ASSERT (!sl_globmatch ("a", "[abc"), "unterminated class never matches");
  SLT_ASSERT (!sl_globmatch (NULL, "abc"), "NULL string returns no match, not a crash");
  SLT_ASSERT (!sl_globmatch ("abc", NULL), "NULL pattern returns no match, not a crash");
  SLT_ASSERT (!sl_globmatch (NULL, NULL), "two NULLs return no match, not a crash");
}

static void
test_streamid_style (void)
{
  /* Patterns in the shape actually used for SeedLink selectors/station IDs */
  SLT_ASSERT (sl_globmatch ("XX_TEST", "XX_TEST"), "exact station id");
  SLT_ASSERT (sl_globmatch ("XX_TEST", "XX_*"), "network wildcard");
  SLT_ASSERT (sl_globmatch ("XX_TEST", "*_TEST"), "station wildcard");
  SLT_ASSERT (sl_globmatch ("00_B_H_Z", "00_B_H_?"), "single wildcard stream id component");
  SLT_ASSERT (!sl_globmatch ("00_B_H_Z", "00_B_H_N"), "distinct stream id rejected");
  SLT_ASSERT (sl_globmatch ("00_B_H_Z", "*_B_H_?"), "combined wildcards on a stream id");
}

int
main (void)
{
  SLT_RUN (test_literals);
  SLT_RUN (test_question);
  SLT_RUN (test_star);
  SLT_RUN (test_charclass);
  SLT_RUN (test_escape);
  SLT_RUN (test_malformed);
  SLT_RUN (test_streamid_style);

  return SLT_REPORT ();
}

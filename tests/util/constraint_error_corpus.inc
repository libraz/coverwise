// Golden corpus for the constraint parser, shared by the C++ and TypeScript tests.
//
// Every surface must accept and reject the same expressions and, for a
// rejection, report the same message and detail. Each entry is
// COVERWISE_CONSTRAINT_CASE(expression, accepted, message, detail); an accepted
// expression carries an empty message and detail. Expressions and texts hold
// neither double quotes nor backslashes, so both readers take them verbatim.
//
// Every entry is parsed against this model, with default (case-insensitive)
// options:
//   os       = win, mac, linux
//   browser  = chrome (alias cr), firefox, safari
//   n        = 1, 2, 10
//
// C++ consumers define COVERWISE_CONSTRAINT_CASE before including this file; the
// TypeScript consumers read it with the regular expression in
// tests/util/constraint-error-corpus.ts.

// Accepted forms.
COVERWISE_CONSTRAINT_CASE("os=win", true, "", "")
COVERWISE_CONSTRAINT_CASE("OS = WIN", true, "", "")
COVERWISE_CONSTRAINT_CASE("'os'=win", true, "", "")
COVERWISE_CONSTRAINT_CASE("os!=linux", true, "", "")
COVERWISE_CONSTRAINT_CASE("os=browser", true, "", "")
COVERWISE_CONSTRAINT_CASE("IF os=win THEN browser!=safari", true, "", "")
COVERWISE_CONSTRAINT_CASE("IF os=mac THEN browser=chrome ELSE browser=firefox", true, "", "")
COVERWISE_CONSTRAINT_CASE("NOT (os=win AND browser=safari)", true, "", "")
COVERWISE_CONSTRAINT_CASE("os=linux IMPLIES browser!=safari", true, "", "")
COVERWISE_CONSTRAINT_CASE("os=win OR browser=chrome", true, "", "")
COVERWISE_CONSTRAINT_CASE("os IN {win, mac}", true, "", "")
COVERWISE_CONSTRAINT_CASE("n > 1", true, "", "")
COVERWISE_CONSTRAINT_CASE("n >= 2 AND n < 10", true, "", "")
COVERWISE_CONSTRAINT_CASE("browser LIKE chrom*", true, "", "")
COVERWISE_CONSTRAINT_CASE("browser LIKE ?irefox", true, "", "")

// A LIKE pattern without a wildcard is an equality test: it resolves aliases
// and case variants, and rejects a value the parameter does not declare.
COVERWISE_CONSTRAINT_CASE("browser LIKE chrome", true, "", "")
COVERWISE_CONSTRAINT_CASE("browser LIKE CHROME", true, "", "")
COVERWISE_CONSTRAINT_CASE("browser LIKE cr", true, "", "")
COVERWISE_CONSTRAINT_CASE("browser LIKE xyz", false, "Unknown value 'xyz' for parameter 'browser'", "Available values: chrome, firefox, safari")

// Rejected forms.
COVERWISE_CONSTRAINT_CASE("", false, "Empty constraint expression", "Provide a non-empty constraint string")
COVERWISE_CONSTRAINT_CASE("os", false, "Expected operator after 'os' at position 2", "Syntax: parameter=value, parameter!=value, parameter>value, parameter IN {values}, or parameter LIKE pattern")
COVERWISE_CONSTRAINT_CASE("os=", false, "Expected value after operator at position 3", "Syntax: parameter=value or parameter!=value")
COVERWISE_CONSTRAINT_CASE("=", false, "Unexpected token '=' at position 0", "Expected a comparison (e.g. param=value) or '('")
COVERWISE_CONSTRAINT_CASE("os=win)", false, "Unexpected token ')' at position 6", "Expected end of expression")
COVERWISE_CONSTRAINT_CASE("os=win AND", false, "Unexpected end of expression", "Expected a comparison or '('")
COVERWISE_CONSTRAINT_CASE("(os=win", false, "Expected ')' at position 7", "Mismatched parentheses")
COVERWISE_CONSTRAINT_CASE("unknown=win", false, "Unknown parameter 'unknown'", "Available parameters: os, browser, n")
COVERWISE_CONSTRAINT_CASE("os=beos", false, "Unknown value 'beos' for parameter 'os'", "Available values: win, mac, linux")
COVERWISE_CONSTRAINT_CASE("IF os=win browser=chrome", false, "Expected 'THEN' after 'IF' clause at position 10", "Syntax: IF <condition> THEN <condition>")
COVERWISE_CONSTRAINT_CASE("os IN win", false, "Expected '{' after 'IN' at position 6", "Syntax: parameter IN {value1, value2, ...}")
COVERWISE_CONSTRAINT_CASE("os IN {}", false, "Expected value in set at position 7", "Syntax: parameter IN {value1, value2, ...}")
COVERWISE_CONSTRAINT_CASE("os IN {win", false, "Expected '}' at position 10", "Syntax: parameter IN {value1, value2, ...}")
COVERWISE_CONSTRAINT_CASE("os IN {win,}", false, "Expected value after ',' at position 11", "Syntax: parameter IN {value1, value2, ...}")
COVERWISE_CONSTRAINT_CASE("browser LIKE", false, "Expected pattern after 'LIKE' at position 12", "Syntax: parameter LIKE pattern (wildcards: * = any string, ? = single char)")
COVERWISE_CONSTRAINT_CASE("os @ win", false, "Unexpected character '@' at position 3", "")
COVERWISE_CONSTRAINT_CASE("os='win", false, "Unterminated string literal starting at position 3", "")

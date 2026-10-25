# grammar

formal grammar for shlcpp. EBNF `|` is alternation, `{ x }` is zero or more,
`[ x ]` is optional, `( x )` is grouping. terminals are `quoted` or CAPS.

this describes the language, not the implementation. the parser does an
indentation pre-pass then parses per line, but the grammar below is what it accepts.

## lexical

```
NEWLINE      ::= '\n'                       (* statement terminator *)
INDENT       ::= (* emitted when indentation increases *)
DEDENT       ::= (* emitted when indentation decreases *)
COMMENT      ::= '#' { any char except '\n' }
             |   '/*' { any char } '*/'         (* shortest match *)

NUMBER       ::= digits [ '.' digits ] [ exponent ]   (* one type: 64-bit float *)
             |   '0' ( 'x' | 'X' | 'b' | 'B' | 'o' | 'O' ) alnum { alnum }
exponent     ::= ( 'e' | 'E' ) [ '+' | '-' ] digits
digits       ::= digit { digit }
letter       ::= 'A'..'Z' | 'a'..'z'
digit        ::= '0'..'9'
alnum        ::= letter | digit
STRING       ::= '"'  { string_char } '"'
             |   '\'' { string_char } '\''
string_char  ::= escape | any char except the delimiting quote and '\'
escape       ::= '\n' | '\t' | '\\' | '\"' | "\'" | ...

IDENT        ::= ( letter | '_' ) { letter | digit | '_' }
keyword      ::= (* any word from the keyword list *)
```

the lexer drops blank lines and comment-only lines before parsing.

keywords (natural word first, programmer alias after):

```
'to' | 'can' | 'def'          function definition
'give' | 'return'             return
'say' | 'show' | 'print'       print line (all three identical)
'esay'                        print line to stderr
'thing' | 'class' | 'structure'
'make' | 'new'                instantiation
'has'                         field declaration
'extends'                     inheritance
'yes' | 'true'                boolean true
'no' | 'false'                boolean false
'null'                        null literal
'if' | 'elif' | 'else'
'while' | 'until' | 'repeat' | 'times' | 'forever' | 'for' | 'in' | 'each'
'stop' | 'skip' | 'pass'
'unless' | 'check' | 'when' | 'otherwise' | 'do'
'try' | 'catch' | 'always' | 'finally'
'error' | 'throw'
'and' | 'or' | 'not'
'spawn' | 'await' | 'parallel'
'import' | 'from' | 'as' | 'use'
'self' | 'super' | 'del' | 'is' | 'be'
'at' | 'least' | 'exactly' | 'less' | 'more' | 'than'
'matches' | 'fn' | 'lambda'
'the' | 'let' | 'please'      (* noise words, skipped by the lexer *)
```

'finally' is a lexer alias for 'always', 'throw' for 'error'.
'unless', 'when' and 'do' are reserved but have no productions yet.

## program

```
program      ::= stmt_list EOF
stmt_list    ::= { stmt }
stmt         ::= simple_stmt ( NEWLINE | EOF ) | compound_stmt
block        ::= NEWLINE INDENT stmt { stmt } DEDENT
header       ::= [ ':' ]                    (* trailing colon optional *)
```

## statements

```
simple_stmt  ::= expr_stmt
             |   'give' [ expr ]             (* 'return' *)
             |   say_stmt
             |   'check' expr
             |   'stop' | 'skip' | 'pass'
             |   'error' expr               (* 'throw' *)
             |   del_stmt
             |   import_stmt

expr_stmt    ::= assign_expr                (* assignment is statement-level *)

say_stmt     ::= say_kw { style_word } expr
say_kw       ::= 'say' | 'show' | 'print' | 'esay'
style_word   ::= 'in' | 'red' | 'green' | 'blue' | 'yellow'
              |  'cyan' | 'magenta' | 'white' | 'black'
              |  'bold' | 'italic' | 'underline'

target       ::= postfix_expr         (* a bare IDENT or 'self', or a postfix
                                     chain ending in index or property;
                                     never a call or literal *)

del_stmt     ::= 'del' target

import_stmt  ::= 'import' IDENT { '.' IDENT } [ 'as' IDENT ]
             |   'from' IDENT { '.' IDENT } 'import' ( '*' | IDENT { ',' IDENT } )
             |   'use' STRING

compound_stmt ::= if_stmt | while_stmt | until_stmt | repeat_stmt
             |   forever_stmt | for_stmt | func_def | class_def
             |   try_stmt | match_stmt | parallel_stmt

if_stmt      ::= 'if' expr header block
                 { 'elif' expr header block }
                 [ 'else' header block ]

while_stmt   ::= 'while' expr header block
until_stmt   ::= 'until' expr header block
repeat_stmt  ::= 'repeat' expr 'times' header block
forever_stmt ::= 'forever' header block
for_stmt     ::= 'for' [ 'each' ] IDENT [ ',' IDENT ] 'in' expr header block

func_def     ::= ( 'to' | 'can' | 'def' ) IDENT '(' [ params ] ')' header block
params       ::= param { ',' param } [ ',' ]
param        ::= IDENT [ '=' expr ]

class_def    ::= ( 'thing' | 'class' | 'structure' ) IDENT
                 [ 'extends' IDENT ] header
                 NEWLINE INDENT class_member { class_member } DEDENT
class_member ::= 'has' IDENT [ '=' expr ] NEWLINE
             |   func_def
             |   'pass' NEWLINE

try_stmt     ::= 'try' header block
                 ( 'catch' IDENT header block [ 'always' header block ]
                 | 'always' header block )

match_stmt   ::= 'matches' expr header NEWLINE INDENT
                 { 'is' expr header block }
                 [ 'otherwise' header block ]
                 DEDENT

parallel_stmt ::= 'parallel' header block
```

## expressions

precedence, low to high. all levels left-associative except assignment
and '\*\*', which are right-associative.

```
expr         ::= or_expr
assign_expr  ::= target assign_op assign_expr | or_expr
assign_op    ::= '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | 'be'
             |   '//=' | '**=' | '<<=' | '>>='
             (* right-assoc; compound ops desugar to x = x op v;
                'be' is the alias in 'let x be 10' *)

or_expr      ::= and_expr { 'or' and_expr }
and_expr     ::= not_expr { 'and' not_expr }
not_expr     ::= 'not' not_expr | bitor_expr
bitor_expr   ::= bitxor_expr { '|' bitxor_expr }
bitxor_expr  ::= bitand_expr { '^' bitand_expr }
bitand_expr  ::= equality_expr { '&' equality_expr }
equality_expr ::= compare_expr { ( '==' | '!=' ) compare_expr }
compare_expr ::= shift_expr { comp_op shift_expr | is_tail }
comp_op      ::= '<' | '<=' | '>' | '>=' | 'in' | 'not' 'in'
is_tail      ::= 'is' ( 'at' 'least' shift_expr
                 | 'exactly' shift_expr
                 | 'less' 'than' shift_expr
                 | 'more' 'than' shift_expr
                 | 'not' 'in' shift_expr
                 | 'in' shift_expr
                 | 'not' shift_expr
                 | shift_expr )                  (* bare 'is' means '==' *)

shift_expr   ::= add_expr { ( '<<' | '>>' ) add_expr }
add_expr     ::= mul_expr { ( '+' | '-' ) mul_expr }
mul_expr     ::= pow_expr { ( '*' | '/' | '//' | '%' ) pow_expr }
pow_expr     ::= unary_expr [ '**' pow_expr ]   (* right-assoc *)
unary_expr   ::= ( '-' | '~' | 'await' | 'spawn' ) unary_expr | postfix_expr

postfix_expr ::= primary { postfix_op }
postfix_op   ::= '(' [ args ] ')'               (* call *)
             |   '[' expr ']'                   (* index *)
             |   '[' [ expr ] ':' [ expr ] [ ':' [ expr ] ] ']'
             |   '.' ( IDENT | keyword )         (* property; keywords work as names *)

primary      ::= NUMBER | STRING | 'yes' | 'no' | 'null'
             |   IDENT | 'self'
             |   '(' assign_expr ')'          (* parens opt back into assignment *)
             |   list_lit | dict_lit | lambda
             |   'make' IDENT '(' [ args ] ')'   (* 'new' *)
             |   'super' '(' [ args ] ')'        (* parent init, subclass only *)
             |   comprehension

list_lit     ::= '[' [ expr { ',' expr } [ ',' ] ] ']'
dict_lit     ::= '{' [ expr ':' expr { ',' expr ':' expr } [ ',' ] ] '}'
lambda       ::= ( 'fn' | 'lambda' ) [ IDENT { ',' IDENT } ] '=>' expr
args         ::= arg { ',' arg } [ ',' ]
arg          ::= IDENT '=' expr | expr
comprehension ::= '[' expr 'for' IDENT 'in' expr [ 'if' expr ] ']'
             (* loop variable is a single IDENT; 'for k, v' doesn't work here *)
```

notes:

- `is` tails: bare `is` is `==`, `is not` is `!=`, `is in` is `in`,
  the rest are the obvious comparison. alternatives are tried in order,
  so `is not` always means `!=`.
- `even`, `odd`, `prime`, `positive` and `negative` are ordinary identifiers;
  there are no `is` trait forms.
- `**` binds tightest of the binary ops and is right-associative:
  `2**3**2` is `2**(3**2)` = 512. unary binds tighter than `**`,
  so `-2**2` is `(-2)**2` = 4, unlike python.
- comparisons are left-associative with no python-style chaining:
  `1 < x < 10` parses as `(1 < x) < 10`.
- assignment is right-associative and returns the value. it is statement-level
  only, plus explicit parens `(x = 5)`.
- `not` binds tighter than `and`/`or` but looser than comparisons.
- `-not x` doesn't parse; write `not -x` or `-(not x)`.
- newlines are ignored inside `()`, `[]` and `{}`.
- a `[` literal is a comprehension iff `for` follows the first expression.
- `super` can only invoke the parent init; an overriding method can't call
  the parent's version (the runtime rejects `super.method()`).
- `x.f(a)` parses as property access followed by a call; the parser
  special-cases it into a method call.
- `keyword` in postfix\_op is any reserved word: `s.matches(re)`,
  `obj.error` and friends parse.
- `assign_expr` tries `target assign_op` first, falling back to `or_expr`.
- the lexer validates digits per radix (`0x` hex, `0b` binary, `0o` octal).
- a missing final NEWLINE is tolerated; EOF ends the last statement.
- `make Dog("rex")` instantiates; `init` runs automatically and returns self.
- `say`, `show`, `print` are identical; `esay` is the stderr variant.
  Style words (`say in red bold "hi"`) are consumed before the expression;
  `say "hi" in red` parses as the comparison `("hi" in red)`, not styling.
  Style words shadow identifiers in say position: `say red` cannot print a
  variable named `red`.


## known deviations

places where the implementation disagrees with the grammar above.

- `not` binds at unary level, tighter than comparisons.
- `-not x` parses (as `-(not x)`).
- assignment is accepted in any expression position, not just statements.
- `await` and `spawn` take a full expression operand.
- a bare `try` block with no `catch` or `always` is accepted.

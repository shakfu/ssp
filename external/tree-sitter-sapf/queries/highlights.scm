; sapf: the token kinds of grammar.js. Words are plain here: the grammar cannot tell a built-in from a name. The
; sapf module colours built-ins from the interpreter's own list (Highlighter::setWords).

(comment) @comment
(string) @string
(number) @number
(each_op) @operator
(quote_symbol) @constant
(backquote_symbol) @function
(dot_symbol) @function.method
(comma_symbol) @function.method
(colon_symbol) @type
["=" "\\"] @keyword
["(" ")" "[" "]" "{" "}" "#["] @punctuation.bracket
(word) @variable

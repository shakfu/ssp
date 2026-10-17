/**
 * Tree-sitter grammar for sapf, for highlighting: a sequence of tokens, as sapf's own lexer
 * (src/engine/Parser.cpp) splits them. Words end at whitespace or one of ;()[]{}.`,:"
 */

const STOP = '\\s;()\\[\\]{}.`,:"';  // what ends a word or symbol name
const NAME = `[^${STOP}]+`;

module.exports = grammar({
  name: 'sapf',

  extras: $ => [/\s+/],

  rules: {
    source_file: $ => repeat($._token),

    _token: $ => choice(
      $.comment,
      $.string,
      $.number,
      $.each_op,
      $.quote_symbol,
      $.backquote_symbol,
      $.dot_symbol,
      $.comma_symbol,
      $.colon_symbol,
      '=',
      '\\',
      '(', ')', '[', ']', '{', '}', '#[',
      $.word,
      token(prec(-1, /\S/)),  // anything else, below every token above
    ),

    comment: $ => /;[^\n]*/,

    string: $ => /"([^"]|"")*"/,

    // sign, digits with a point, exponent, a scale suffix (pi M k h c m u), and a denominator; pi alone;
    // hex. The longest match wins, so 2x is a word, as in sapf; at equal length, number is declared before
    // word, so 440 and 1/2 are numbers. A prec() here would rank above length.
    number: $ => token(choice(
      /[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?(pi|[Mkhcmu])?(\/(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?(pi|[Mkhcmu])?)?/,
      /[+-]?pi/,
      /0x[0-9a-fA-F]+/,
    )),

    // @, @@..., @2..@9, @0101
    each_op: $ => /@(@+|[2-9]|[01]+)?/,

    quote_symbol: $ => new RegExp(`'${NAME}`),
    backquote_symbol: $ => new RegExp(`\`${NAME}`),
    dot_symbol: $ => new RegExp(`\\.${NAME}`),
    comma_symbol: $ => new RegExp(`,${NAME}`),
    colon_symbol: $ => new RegExp(`:${NAME}`),

    // the first character is not one that starts another token
    word: $ => new RegExp(`[^${STOP}@'\\\\#][^${STOP}]*`),
  },
});

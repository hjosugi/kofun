#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum TokenType {
  NEWLINE,
  SOFT_NEWLINE,
  LEADING_PIPELINE,
  LEADING_ELSE,
  LEFT_PAREN,
  RIGHT_PAREN,
  LEFT_BRACKET,
  RIGHT_BRACKET,
};

typedef struct {
  uint16_t delimiter_depth;
} Scanner;

void *tree_sitter_kofun_external_scanner_create(void) {
  return calloc(1, sizeof(Scanner));
}

void tree_sitter_kofun_external_scanner_destroy(void *payload) {
  free(payload);
}

unsigned tree_sitter_kofun_external_scanner_serialize(
    void *payload,
    char *buffer) {
  const Scanner *scanner = payload;
  buffer[0] = (char)(scanner->delimiter_depth & UINT16_C(0xff));
  buffer[1] = (char)(scanner->delimiter_depth >> 8);
  return 2;
}

void tree_sitter_kofun_external_scanner_deserialize(
    void *payload,
    const char *buffer,
    unsigned length) {
  Scanner *scanner = payload;
  scanner->delimiter_depth = 0;
  if (length >= 2) {
    scanner->delimiter_depth =
        (uint16_t)(unsigned char)buffer[0] |
        (uint16_t)((uint16_t)(unsigned char)buffer[1] << 8);
  }
}

static bool scan_newline_or_pipeline(
    Scanner *scanner,
    TSLexer *lexer,
    const bool *valid_symbols) {
  if (lexer->lookahead != '\n' && lexer->lookahead != '\r') {
    return false;
  }

  const bool is_soft = scanner->delimiter_depth > 0;
  if (is_soft && !valid_symbols[SOFT_NEWLINE]) {
    return false;
  }
  if (!is_soft &&
      !valid_symbols[NEWLINE] &&
      !valid_symbols[LEADING_PIPELINE] &&
      !valid_symbols[LEADING_ELSE]) {
    return false;
  }

  if (lexer->lookahead == '\r') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '\n') {
      lexer->advance(lexer, false);
    }
  } else {
    lexer->advance(lexer, false);
  }
  lexer->mark_end(lexer);

  if (!is_soft &&
      (valid_symbols[LEADING_PIPELINE] || valid_symbols[LEADING_ELSE])) {
    for (;;) {
      while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        lexer->advance(lexer, true);
      }
      if (lexer->lookahead != '\n' && lexer->lookahead != '\r') {
        break;
      }
      if (lexer->lookahead == '\r') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '\n') {
          lexer->advance(lexer, true);
        }
      } else {
        lexer->advance(lexer, true);
      }
    }
    if (valid_symbols[LEADING_PIPELINE] && lexer->lookahead == '|') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '>') {
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        lexer->result_symbol = LEADING_PIPELINE;
        return true;
      }
    }
    if (valid_symbols[LEADING_ELSE] && lexer->lookahead == 'e') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == 'l') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == 's') {
          lexer->advance(lexer, false);
          if (lexer->lookahead == 'e') {
            lexer->advance(lexer, false);
            const int32_t next = lexer->lookahead;
            const bool continues_identifier =
                next == '_' ||
                (next >= '0' && next <= '9') ||
                (next >= 'A' && next <= 'Z') ||
                (next >= 'a' && next <= 'z') ||
                next >= 0x80;
            if (!continues_identifier) {
              lexer->mark_end(lexer);
              lexer->result_symbol = LEADING_ELSE;
              return true;
            }
          }
        }
      }
    }
  }

  if (is_soft) {
    lexer->result_symbol = SOFT_NEWLINE;
    return true;
  }
  if (valid_symbols[NEWLINE]) {
    lexer->result_symbol = NEWLINE;
    return true;
  }
  return false;
}

bool tree_sitter_kofun_external_scanner_scan(
    void *payload,
    TSLexer *lexer,
    const bool *valid_symbols) {
  Scanner *scanner = payload;

  while (lexer->lookahead == ' ' ||
         lexer->lookahead == '\t' ||
         lexer->lookahead == '\f' ||
         lexer->lookahead == '\v') {
    lexer->advance(lexer, true);
  }

  if (scan_newline_or_pipeline(scanner, lexer, valid_symbols)) {
    return true;
  }

  enum TokenType token;
  bool opens = false;
  bool closes = false;
  switch (lexer->lookahead) {
    case '(':
      token = LEFT_PAREN;
      opens = true;
      break;
    case ')':
      token = RIGHT_PAREN;
      closes = true;
      break;
    case '[':
      token = LEFT_BRACKET;
      opens = true;
      break;
    case ']':
      token = RIGHT_BRACKET;
      closes = true;
      break;
    default:
      return false;
  }

  if (!valid_symbols[token]) {
    return false;
  }

  lexer->advance(lexer, false);
  lexer->result_symbol = token;

  if (opens && scanner->delimiter_depth < UINT16_MAX) {
    scanner->delimiter_depth++;
  } else if (closes && scanner->delimiter_depth > 0) {
    scanner->delimiter_depth--;
  }
  return true;
}

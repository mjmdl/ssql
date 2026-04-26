typedef enum Report_Level {
    REPORT_LEVEL__DEBUG,
    REPORT_LEVEL__INFO,
    REPORT_LEVEL__WARNING,
    REPORT_LEVEL__ERROR,

    REPORT_LEVEL_COUNT_,
} Report_Level;

typedef struct Report {
    Report_Level level;
    size_t line;
    size_t column;
    size_t position;
    const char *message;
} Report;

typedef struct Reporter {
    Array reports;
    Arena messages;
    size_t errors;
    size_t errors_maximum;
} Reporter;

typedef enum Token_Kind {
    TOKEN_KIND__NONE = 0,
    TOKEN_KIND__END_OF_FILE,

    TOKEN_KIND__IDENTIFIER,
    TOKEN_KIND__QUOTED_IDENTIFIER,
    TOKEN_KIND__LITERAL_STRING,
    TOKEN_KIND__LITERAL_NUMBER,

    TOKEN_KIND__PARENTHESIS_LEFT,
    TOKEN_KIND__PARENTHESIS_RIGHT,
    TOKEN_KIND__COMMA,
    TOKEN_KIND__DOT,
    TOKEN_KIND__SEMICOLON,

    TOKEN_KIND__PLUS,
    TOKEN_KIND__MINUS,
    TOKEN_KIND__STAR,
    TOKEN_KIND__SLASH,
    TOKEN_KIND__EQUAL,
    TOKEN_KIND__LESS,
    TOKEN_KIND__LESS_EQUAL,
    TOKEN_KIND__DIAMOND,
    TOKEN_KIND__GREATER,
    TOKEN_KIND__GREATER_EQUAL,
    TOKEN_KIND__PIPE_PIPE,

    TOKEN_KIND__AS,
    TOKEN_KIND__BY,
    TOKEN_KIND__IN,
    TOKEN_KIND__IS,
    TOKEN_KIND__ON,
    TOKEN_KIND__OR,
    TOKEN_KIND__AND,
    TOKEN_KIND__CASE,
    TOKEN_KIND__CAST,
    TOKEN_KIND__ELSE,
    TOKEN_KIND__FROM,
    TOKEN_KIND__FULL,
    TOKEN_KIND__LEFT,
    TOKEN_KIND__NEXT,
    TOKEN_KIND__NULL,
    TOKEN_KIND__ONLY,
    TOKEN_KIND__OVER,
    TOKEN_KIND__ROWS,
    TOKEN_KIND__TRUE,
    TOKEN_KIND__WHEN,
    TOKEN_KIND__WITH,
    TOKEN_KIND__FALSE,
    TOKEN_KIND__FETCH,
    TOKEN_KIND__FIRST,
    TOKEN_KIND__GROUP,
    TOKEN_KIND__INNER,
    TOKEN_KIND__LIMIT,
    TOKEN_KIND__ORDER,
    TOKEN_KIND__OUTER,
    TOKEN_KIND__RIGHT,
    TOKEN_KIND__WHERE,
    TOKEN_KIND__OFFSET,
    TOKEN_KIND__SELECT,
    TOKEN_KIND__PERCENT,
    TOKEN_KIND__DISTINCT,
    TOKEN_KIND__PARTITION,

    TOKEN_KIND_COUNT_,
} Token_Kind;

typedef struct Token {
    Token_Kind kind;
    const char *lexeme;
    size_t length;
    size_t position;
    size_t line;
    size_t column;
} Token;

typedef struct Lexer {
    const char *file_name;
    const char *begin;
    const char *end;
    const char *at;
    const char *line_begin;
    size_t line;
    Array tokens;
    Arena lexemes;
    Reporter reporter;
} Lexer;

static Lexer lexer_create(const char *file_name, const char *source, size_t length) {
    return (Lexer){
        .file_name = file_name,
        .begin = source,
        .end = source + length,
        .at = source,
        .line_begin = source,
        .line = 1,
        .tokens = array_create(sizeof (Token), 512),
        .lexemes = arena_create(sizeof (char) * 1024 * 10),
        .reporter = (Reporter){
            .reports = (Array){
                .stride = sizeof (Report),
            },
            .messages = (Arena){0},
            .errors = 0,
            .errors_maximum = 10,
        },
    };
}

static void lexer_destroy(Lexer *lexer) {
    array_destroy(&lexer->tokens);
    arena_destroy(&lexer->lexemes);
    array_destroy(&lexer->reporter.reports);
    arena_destroy(&lexer->reporter.messages);
    *lexer = (Lexer){0};
}

static size_t string_length(const char *string) {
    for (size_t i = 0;; i += 1) {
        if (string[i] == '\0') {
            return i;
        }
    }
}

#if defined (__GNUC__) || defined (__clang__)
#   define PRINTF_FUNCTION(FORMAT_POSITION, VAARGS_POSITION) __attribute__((format(printf, FORMAT_POSITION, VAARGS_POSITION)))
#   define PRINTF_PARAMETER()
#elif defined (_MSC_VER)
#   define PRINTF_FUNCTION(FORMAT_POSITION, VAARGS_POSITION)
#   define PRINTF_PARAMETER() _Printf_format_string_
#else
#   define PRINTF_FUNCTION(FORMAT_POSITION, VAARGS_POSITION)
#   define PRINTF_PARAMETER()
#endif

PRINTF_FUNCTION(4, 5)
static void lexer_report_at(Lexer *lexer, Report_Level level, const char *at, PRINTF_PARAMETER() const char *format, ...) {
    assert(lexer->begin <= at && at <= lexer->end && lexer->line_begin <= at);
    
    if (level == REPORT_LEVEL__ERROR) {
        if (lexer->reporter.errors >= lexer->reporter.errors_maximum) {
            return;
        }
        
        lexer->reporter.errors += 1;
    }

    if (lexer->reporter.messages.block == NULL) {
        lexer->reporter.messages = arena_create(1024 * sizeof (char));
    }

    char buffer[1024];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    if (n >= (int)(sizeof buffer)) {
        buffer[sizeof buffer - 4] = '.';
        buffer[sizeof buffer - 3] = '.';
        buffer[sizeof buffer - 2] = '.';
    }
    
    Report *report = array_allocate(&lexer->reporter.reports);
    *report = (Report){
        .level = level,
        .line = lexer->line,
        .column = at - lexer->line_begin + 1,
        .position = at - lexer->begin,
        .message = arena_duplicate_string(&lexer->reporter.messages, buffer, string_length(buffer)),
    };
}

static bool is_digit(char rune) {
    return '0' <= rune && rune <= '9';
}

static bool can_be_name_initial(char rune) {
    return ('a' <= rune && rune <= 'z') || ('A' <= rune && rune <= 'Z') || rune == '_';
}

static bool can_be_name_stem(char rune) {
    return can_be_name_initial(rune) || ('0' <= rune && rune <= '9');
}

static bool lexer_test_literal_number(Lexer *lexer, const char **out_lexeme, size_t *out_length) {
    if (lexer->at >= lexer->end) {
        return false;
    }
    
    bool fractional = false;
    if (!is_digit(lexer->at[0])) {
        fractional = lexer->at[0] == '.' && (lexer->at + 1) < lexer->end && is_digit(lexer->at[1]);
        if (!fractional) {
            return false;
        }
    }

    const char *at = lexer->at;
    bool scientific_notation = false;

    do {
        at += 1;
        
        if (!is_digit(at[0])) {
            if (at[0] == '.') {
                if (fractional) {
                    lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "a literal number cannot have more than one dot");
                    continue;
                }
            
                if (scientific_notation) {
                    lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "scientific notation does not support fractional exponent");
                    continue;
                }
            
                if ((at + 1) < lexer->end && !is_digit(at[1])) {
                    lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "expected digit after the dot");
                    continue;
                }
            
                fractional = true;
            } else if (at[0] == 'e') {
                if (scientific_notation) {
                    lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "scientific notation can not be nested");
                    continue;
                }
            
                at += 1;
                if (at >= lexer->end) {
                    lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "early end of file, expected a digit or sign");
                    continue;
                }
                
                scientific_notation = true;
                fractional = false;

                if (!is_digit(at[0])) {
                    if (at[0] == '+' || at[0] == '-') {
                        if ((at + 1) >= lexer->end) {
                            lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "early end of file, expected a digit or sign");
                            continue;
                        }
                
                        if (!is_digit(at[1])) {
                            lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "expected a digit after the sign");
                            continue;
                        }
                
                        at += 1;
                    } else {
                        lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "expected a digit or sign after the 'e'");
                        continue;
                    }
                }
            } else if (can_be_name_initial(at[0])) {
                lexer_report_at(lexer, REPORT_LEVEL__ERROR, at, "expected a space between number literal and identifier");
                while (at < lexer->end && can_be_name_stem(at[0])) {
                    at += 1;
                }
                break;
            } else {
                break;
            }
        }
    } while (at < lexer->end);

    *out_lexeme = lexer->at;
    *out_length = at - lexer->at;
    return true;
}

static bool lexer_test_name(const Lexer *lexer, const char **out_lexeme, size_t *out_length) {
    if (lexer->at >= lexer->end || !can_be_name_initial(lexer->at[0])) {
        return false;
    }
    
    const char *begin = lexer->at;
    const char *end = lexer->end;
    const char *at = lexer->at + 1;

    while (at < end && can_be_name_stem(at[0])) {
        at += 1;
    }
    
    *out_lexeme = begin;
    *out_length = at - begin;
    return true;
}

static bool lexer_test_delimited_string(const Lexer *lexer, char delimiter, const char **out_lexeme, size_t *out_length) {
    if (lexer->at >= lexer->end || lexer->at[0] != delimiter) {
        return false;
    }
    
    const char *at = lexer->at + 1;

    bool delimiter_found = false;
    while (at < lexer->end) {
        if (delimiter_found) {
            if (at[0] != delimiter) {
                break;
            }
            
            delimiter_found = false;
        } else if (at[0] == delimiter) {
            delimiter_found = true;
        }
        
        at += 1;
    }
    
    *out_lexeme = lexer->at;
    *out_length = at - lexer->at;
    return true;
}

static char to_lowercase(char rune) {
    return ('A' <= rune && rune <= 'Z') ? rune + ('a' - 'A') : rune;
}

static bool strings_equal_caseless(const char *one, const char *two, size_t length) {
    for (size_t i = 0; i < length; i += 1) {
        if (to_lowercase(one[i]) != to_lowercase(two[i])) {
            return false;
        }
    }
    return true;
}

static Token_Kind test_keywords(const char *lexeme, size_t length) {
    switch (length) {
    case 2:
        if (strings_equal_caseless("as", lexeme, 2)) return TOKEN_KIND__AS;
        if (strings_equal_caseless("by", lexeme, 2)) return TOKEN_KIND__BY;
        if (strings_equal_caseless("in", lexeme, 2)) return TOKEN_KIND__IN;
        if (strings_equal_caseless("is", lexeme, 2)) return TOKEN_KIND__IS;
        if (strings_equal_caseless("on", lexeme, 2)) return TOKEN_KIND__ON;
        if (strings_equal_caseless("or", lexeme, 2)) return TOKEN_KIND__OR;
        break;
    case 3:
        if (strings_equal_caseless("and", lexeme, 3)) return TOKEN_KIND__AND;
        break;
    case 4:
        if (strings_equal_caseless("case", lexeme, 4)) return TOKEN_KIND__CASE;
        if (strings_equal_caseless("cast", lexeme, 4)) return TOKEN_KIND__CAST;
        if (strings_equal_caseless("else", lexeme, 4)) return TOKEN_KIND__ELSE;
        if (strings_equal_caseless("from", lexeme, 4)) return TOKEN_KIND__FROM;
        if (strings_equal_caseless("full", lexeme, 4)) return TOKEN_KIND__FULL;
        if (strings_equal_caseless("left", lexeme, 4)) return TOKEN_KIND__LEFT;
        if (strings_equal_caseless("next", lexeme, 4)) return TOKEN_KIND__NEXT;
        if (strings_equal_caseless("null", lexeme, 4)) return TOKEN_KIND__NULL;
        if (strings_equal_caseless("only", lexeme, 4)) return TOKEN_KIND__ONLY;
        if (strings_equal_caseless("over", lexeme, 4)) return TOKEN_KIND__OVER;
        if (strings_equal_caseless("rows", lexeme, 4)) return TOKEN_KIND__ROWS;
        if (strings_equal_caseless("true", lexeme, 4)) return TOKEN_KIND__TRUE;
        if (strings_equal_caseless("when", lexeme, 4)) return TOKEN_KIND__WHEN;
        if (strings_equal_caseless("with", lexeme, 4)) return TOKEN_KIND__WITH;
        break;
    case 5:
        if (strings_equal_caseless("false", lexeme, 5)) return TOKEN_KIND__FALSE;
        if (strings_equal_caseless("fetch", lexeme, 5)) return TOKEN_KIND__FETCH;
        if (strings_equal_caseless("first", lexeme, 5)) return TOKEN_KIND__FIRST;
        if (strings_equal_caseless("group", lexeme, 5)) return TOKEN_KIND__GROUP;
        if (strings_equal_caseless("inner", lexeme, 5)) return TOKEN_KIND__INNER;
        if (strings_equal_caseless("limit", lexeme, 5)) return TOKEN_KIND__LIMIT;
        if (strings_equal_caseless("order", lexeme, 5)) return TOKEN_KIND__ORDER;
        if (strings_equal_caseless("outer", lexeme, 5)) return TOKEN_KIND__OUTER;
        if (strings_equal_caseless("right", lexeme, 5)) return TOKEN_KIND__RIGHT;
        if (strings_equal_caseless("where", lexeme, 5)) return TOKEN_KIND__WHERE;
        break;
    case 6:
        if (strings_equal_caseless("offset", lexeme, 6)) return TOKEN_KIND__OFFSET;
        if (strings_equal_caseless("select", lexeme, 6)) return TOKEN_KIND__SELECT;
        break;
    case 7:
        if (strings_equal_caseless("percent", lexeme, 7)) return TOKEN_KIND__PERCENT;
        break;
    case 8:
        if (strings_equal_caseless("distinct", lexeme, 8)) return TOKEN_KIND__DISTINCT;
        break;
    case 9:
        if (strings_equal_caseless("partition", lexeme, 9)) return TOKEN_KIND__PARTITION;
        break;
    }
    return TOKEN_KIND__NONE;
}

static Token_Kind test_operators(const char *lexeme, size_t max_length, size_t *out_length) {
    if (max_length == 0) return TOKEN_KIND__NONE;
    switch (lexeme[0]) {
    case '(': *out_length = 1; return TOKEN_KIND__PARENTHESIS_LEFT;
    case ')': *out_length = 1; return TOKEN_KIND__PARENTHESIS_RIGHT;
    case ',': *out_length = 1; return TOKEN_KIND__COMMA;
    case '.': *out_length = 1; return TOKEN_KIND__DOT;
    case ';': *out_length = 1; return TOKEN_KIND__SEMICOLON;
    case '+': *out_length = 1; return TOKEN_KIND__PLUS;
    case '-': *out_length = 1; return TOKEN_KIND__MINUS;
    case '*': *out_length = 1; return TOKEN_KIND__STAR;
    case '/': *out_length = 1; return TOKEN_KIND__SLASH;
    case '=': *out_length = 1; return TOKEN_KIND__EQUAL;
    case '!':
        if (max_length >= 2 && lexeme[1] == '=') { *out_length = 2; return TOKEN_KIND__DIAMOND; }
        break;
    case '<':
        if (max_length >= 2) {
            if (lexeme[1] == '=') { *out_length = 2; return TOKEN_KIND__LESS_EQUAL; }
            if (lexeme[1] == '>') { *out_length = 2; return TOKEN_KIND__DIAMOND; }
        }
        *out_length = 1; return TOKEN_KIND__LESS;
    case '>':
        if (max_length >= 2 && lexeme[1] == '=') { *out_length = 2; return TOKEN_KIND__LESS_EQUAL; }
        *out_length = 1; return TOKEN_KIND__LESS;
    case '|':
        if (max_length >= 2 && lexeme[1] == '|') { *out_length = 2; return TOKEN_KIND__PIPE_PIPE; }
        break;
    }
    return TOKEN_KIND__NONE;
}

static void lexer_skip_whitespace(Lexer *lexer) {
    while (lexer->at < lexer->end) {
        if (lexer->at[0] == ' ' || lexer->at[0] == '\t' || lexer->at[0] == '\r') {
            lexer->at += 1;
        } else if (lexer->at[0] == '\n') {
            lexer->at += 1;
            lexer->line += 1;
            lexer->line_begin = lexer->at;
        } else {
            break;
        }
    }
}

static bool lexer_skip_comment(Lexer *lexer) {
    if ((lexer->at + 1) >= lexer->end) {
        return false;
    }
    
    if (lexer->at[0] == '-' && lexer->at[1] == '-') {
        lexer->at += 2;
        while (lexer->at < lexer->end && lexer->at[0] != '\n') {
            lexer->at += 1;
        }
        return true;
    }
    
    if (lexer->at[0] == '/' && lexer->at[1] == '*') {
        lexer->at += 2;
        for (;;) {
            if ((lexer->at + 1) >= lexer->end) {
                lexer_report_at(lexer, REPORT_LEVEL__WARNING, lexer->at, "unclosed comment");
                break;
            }
            
            if (lexer->at[0] == '*' && lexer->at[1] == '/') {
                lexer->at += 2;
                break;
            }

            if (lexer->at[0] == '\n') {
                lexer->line += 1;
                lexer->line_begin = lexer->at + 1;
            }
            
            lexer->at += 1;
        }
        return true;
    }
    
    return false;
}

static Token *lexer_create_token(Lexer *lexer, Token_Kind kind, const char *lexeme, size_t length) {
    Token *token = array_allocate(&lexer->tokens);
    *token = (Token){
        .kind = kind,
        .lexeme = lexeme == NULL ? NULL : arena_duplicate_string(&lexer->lexemes, lexeme, length),
        .length = length,
        .position = lexer->at - lexer->begin,
        .line = lexer->line,
        .column = lexer->at - lexer->line_begin + 1,
    };
    return token;
}

static Token *lexer_next_token(Lexer *lexer) {
    do {
        lexer_skip_whitespace(lexer);
    } while (lexer_skip_comment(lexer));
    
    if (lexer->at >= lexer->end) {
        return lexer_create_token(lexer, TOKEN_KIND__END_OF_FILE, NULL, 0);
    }

    if (lexer->reporter.errors >= lexer->reporter.errors_maximum) {
        return lexer_create_token(lexer, TOKEN_KIND__END_OF_FILE, NULL, 0);
    }

    Token *token = NULL;
    Token_Kind kind = TOKEN_KIND__NONE;
    const char *lexeme = NULL;
    size_t length = 0;

    if (lexer_test_name(lexer, &lexeme, &length)) {
        if ((kind = test_keywords(lexeme, length)) != TOKEN_KIND__NONE) {
            token = lexer_create_token(lexer, kind, NULL, length);
        } else {
            token = lexer_create_token(lexer, TOKEN_KIND__IDENTIFIER, lexeme, length);
        }
    } else if (lexer_test_delimited_string(lexer, '"', &lexeme, &length)) {
        token = lexer_create_token(lexer, TOKEN_KIND__QUOTED_IDENTIFIER, lexeme + 1, length - 2);
    } else if (lexer_test_delimited_string(lexer, '\'', &lexeme, &length)) {
        token =  lexer_create_token(lexer, TOKEN_KIND__LITERAL_STRING, lexeme + 1, length - 2);
    } else if (lexer_test_literal_number(lexer, &lexeme, &length)) {
        token = lexer_create_token(lexer, TOKEN_KIND__LITERAL_NUMBER, lexeme, length);
    } else if ((kind = test_operators(lexer->at, lexer->end - lexer->at, &length)) != TOKEN_KIND__NONE) {
        token = lexer_create_token(lexer, kind, NULL, length);
    } else {
        lexer_report_at(lexer, REPORT_LEVEL__ERROR, lexer->at, "unexpected character '%c'", lexer->at[0]);
        length = 1;
        token = lexer_create_token(lexer, TOKEN_KIND__NONE, NULL, length);
    }

    lexer->at += length;
    return token;
}

static void lexer_tokenize(Lexer *lexer) {
    Token *token;
    do {
        token = lexer_next_token(lexer);
    } while (token->kind != TOKEN_KIND__END_OF_FILE);
}

static void lexer_print_report(const Lexer *lexer, const Report *report, FILE *stream) {
    static const char *const LEVEL_NAMES[REPORT_LEVEL_COUNT_] = {
        [REPORT_LEVEL__DEBUG] = "debug",
        [REPORT_LEVEL__INFO] = "info",
        [REPORT_LEVEL__WARNING] = "warning",
        [REPORT_LEVEL__ERROR] = "error",
    };
    const char *level_name = LEVEL_NAMES[report->level];

    const char *line_begin = lexer->begin + report->position;
    for (; line_begin > lexer->begin && line_begin[-1] != '\n'; line_begin -= 1);
    const char *line_end = line_begin;
    for (; line_end < lexer->end && line_end[0] != '\n'; line_end += 1);

    fprintf(stream, "%s at %s:%zu:%zu, pos %zu: %s\n", level_name, lexer->file_name, report->line, report->column, report->position + 1, report->message);
    fprintf(stream, "      |%*sv\n", (int)report->column, " ");
    fprintf(stream, "%5zu | %.*s\n", report->line, (int)(line_end - line_begin), line_begin);
}

static void lexer_print_reports(const Lexer *lexer, FILE *stream) {
    if (lexer->reporter.errors != 0) {
        printf("INFO: x%zu errors occurred.\n", lexer->reporter.errors);
    }
    for (size_t i = 0; i < lexer->reporter.reports.count; i += 1) {
        lexer_print_report(lexer, (Report *)lexer->reporter.reports.elements + i, stream);
        putc('\n', stdout);
    }
}

static void token_to_string(const Token *token, char *buffer, size_t max_length) {
    static const char *const KIND_NAMES[TOKEN_KIND_COUNT_] = {
        [TOKEN_KIND__NONE] = "none",
        [TOKEN_KIND__END_OF_FILE] = "end of file",

        [TOKEN_KIND__IDENTIFIER] = "identifier",
        [TOKEN_KIND__QUOTED_IDENTIFIER] = "quoted identifier",
        [TOKEN_KIND__LITERAL_STRING] = "literal string",
        [TOKEN_KIND__LITERAL_NUMBER] = "literal number",

        [TOKEN_KIND__PARENTHESIS_LEFT] = "parenthesis left [(]",
        [TOKEN_KIND__PARENTHESIS_RIGHT] = "parenthesis right [)]",
        [TOKEN_KIND__COMMA] = "comma [,]",
        [TOKEN_KIND__DOT] = "dot [.]",
        [TOKEN_KIND__SEMICOLON] = "semicolon [;]",

        [TOKEN_KIND__PLUS] = "plus [+]",
        [TOKEN_KIND__MINUS] = "minus [-]",
        [TOKEN_KIND__STAR] = "star [*]",
        [TOKEN_KIND__SLASH] = "slash [/]",
        [TOKEN_KIND__EQUAL] = "equal [=]",
        [TOKEN_KIND__LESS] = "less [<]",
        [TOKEN_KIND__LESS_EQUAL] = "less equal [<=]",
        [TOKEN_KIND__DIAMOND] = "diamond [<>]",
        [TOKEN_KIND__GREATER] = "greater [>]",
        [TOKEN_KIND__GREATER_EQUAL] = "greater equal [>=]",
        [TOKEN_KIND__PIPE_PIPE] = "pipe pipe [||]",

        [TOKEN_KIND__AS] = "AS",
        [TOKEN_KIND__BY] = "BY",
        [TOKEN_KIND__IN] = "IN",
        [TOKEN_KIND__IS] = "IS",
        [TOKEN_KIND__ON] = "ON",
        [TOKEN_KIND__OR] = "OR",
        [TOKEN_KIND__AND] = "AND",
        [TOKEN_KIND__CASE] = "CASE",
        [TOKEN_KIND__CAST] = "CAST",
        [TOKEN_KIND__ELSE] = "ELSE",
        [TOKEN_KIND__FROM] = "FROM",
        [TOKEN_KIND__FULL] = "FULL",
        [TOKEN_KIND__LEFT] = "LEFT",
        [TOKEN_KIND__NEXT] = "NEXT",
        [TOKEN_KIND__NULL] = "NULL",
        [TOKEN_KIND__ONLY] = "ONLY",
        [TOKEN_KIND__OVER] = "OVER",
        [TOKEN_KIND__ROWS] = "ROWS",
        [TOKEN_KIND__TRUE] = "TRUE",
        [TOKEN_KIND__WHEN] = "WHEN",
        [TOKEN_KIND__WITH] = "WITH",
        [TOKEN_KIND__FALSE] = "FALSE",
        [TOKEN_KIND__FETCH] = "FETCH",
        [TOKEN_KIND__FIRST] = "FIRST",
        [TOKEN_KIND__GROUP] = "GROUP",
        [TOKEN_KIND__INNER] = "INNER",
        [TOKEN_KIND__LIMIT] = "LIMIT",
        [TOKEN_KIND__ORDER] = "ORDER",
        [TOKEN_KIND__OUTER] = "OUTER",
        [TOKEN_KIND__RIGHT] = "RIGHT",
        [TOKEN_KIND__WHERE] = "WHERE",
        [TOKEN_KIND__OFFSET] = "OFFSET",
        [TOKEN_KIND__SELECT] = "SELECT",
        [TOKEN_KIND__PERCENT] = "PERCENT",
        [TOKEN_KIND__DISTINCT] = "DISTINCT",
        [TOKEN_KIND__PARTITION] = "PARTITION",
    };
    const char *kind_name = KIND_NAMES[token->kind];
        
    size_t printed = snprintf(buffer, max_length, "%s", kind_name);
    
    switch (token->kind) {
    case TOKEN_KIND__IDENTIFIER: printed += snprintf(buffer + printed, max_length - printed, " (%.*s)", (int)token->length, token->lexeme); break;
    case TOKEN_KIND__QUOTED_IDENTIFIER: printed += snprintf(buffer + printed, max_length - printed, " \"%.*s\"", (int)token->length, token->lexeme); break;
    case TOKEN_KIND__LITERAL_STRING: printed += snprintf(buffer + printed, max_length - printed, " '%.*s'", (int)token->length, token->lexeme); break;
    case TOKEN_KIND__LITERAL_NUMBER: printed += snprintf(buffer + printed, max_length - printed, " %.*s", (int)token->length, token->lexeme); break;
    default: break;
    }

    buffer[printed] = '\0';
}

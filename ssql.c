#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void unused() {}

#define UNUSED(...) unused(__VA_ARGS__)
#define UNREACHABLE() assert(false && "Unreachable!")
#define UNIMPLEMENTED() assert(false && "Unimplemented!")

typedef struct Arena {
    struct Arena *next;
    size_t allocated;
    size_t used;
    uint8_t data[];
} Arena;

typedef enum Token_Kind {
    Token_Kind__None = 0,

    Token_Kind__Identifier, /* table_name or "Table Name" */
    Token_Kind__Literal_Number, /* 3.14 */
    Token_Kind__Literal_Text, /* 'abc' */

    Token_Kind__Asterisk, /* * */
    Token_Kind__Comma, /* , */
    Token_Kind__Dot, /* . */
    Token_Kind__Equals, /* = or == */
    Token_Kind__Greater, /* > */
    Token_Kind__Greater_Equals, /* >= */
    Token_Kind__Lesser, /* < */
    Token_Kind__Lesser_Equals, /* <= */
    Token_Kind__Minus, /* - */
    Token_Kind__Not_Equals, /* <> or != */
    Token_Kind__Parenthesis_Close, /* ) */
    Token_Kind__Parenthesis_Open, /* ( */
    Token_Kind__Double_Pipe, /* || */
    Token_Kind__Plus, /* + */
    Token_Kind__Semicolon, /* ; */
    Token_Kind__Slash, /* / */
} Token_Kind;

typedef struct Token {
    Token_Kind  kind;
    size_t position;
    uint32_t line;
    uint32_t offset;
    char *literal;
    size_t literal_length;
} Token;

typedef struct Lexer {
    const char *begin;
    const char *end;
    const char *head;
    const char *line_start;
    size_t line;
    Token *tokens;
    size_t tokens_used;
    size_t tokens_allocated;
    Token *next_token;
    Arena *strings;
} Lexer;

typedef enum Lexer_Status {
    Lexer_Status__Unclosed_String = -5,
    Lexer_Status__Invalid_Number = -4,
    Lexer_Status__Invalid_String = -3,
    Lexer_Status__Unclosed_Comment_Block = -2,
    Lexer_Status__Unexpected_Character = -1,
    Lexer_Status__Ok = 0,
    Lexer_Status__Token_Found = 1,
} Lexer_Status;

Arena *arena_create(size_t size)
{
    Arena *arena = malloc(sizeof *arena + size);
    assert(arena != NULL);
    arena->next = NULL;
    arena->allocated = size;
    arena->used = 0;
    return arena;
}

void arena_destroy(Arena *arena)
{
    if (arena->next != NULL) arena_destroy(arena->next);
    free(arena);
}

void *arena_allocate_aligned(Arena *arena, size_t size, size_t alignment)
{
    size_t offset = (alignment - (arena->used & (alignment - 1))) & (alignment - 1);

    size_t space = (arena->allocated - arena->used) - offset;
    if (space < size) {
        if (arena->next == NULL) arena->next = arena_create(size > arena->allocated ? size : arena->allocated);
        return arena_allocate_aligned(arena->next, size, alignment);
    }

    void *position = &arena->data[arena->used + offset];
    arena->used += size + offset;
    return position;
}

void *arena_allocate(Arena *arena, size_t size)
{
    typedef union { long l; double d; void *p; } Max_Align;
    
    return arena_allocate_aligned(arena, size, sizeof (Max_Align));
}

char *arena_duplicate_string(Arena *arena, const char *from, size_t length)
{
    char *into = arena_allocate_aligned(arena, length + 1, 1);
    for (size_t i = 0; i < length; ++i) into[i] = from[i];
    into[length] = '\0';
    return into;
}

const char *lexer_status_name(Lexer_Status status)
{
    switch (status) {
    case Lexer_Status__Unclosed_String: return "Unclosed_String";
    case Lexer_Status__Invalid_Number: return "Invalid_Number";
    case Lexer_Status__Invalid_String: return "Invalid_String";
    case Lexer_Status__Unclosed_Comment_Block: return "Unclosed_Comment_Block";
    case Lexer_Status__Unexpected_Character: return "Unexpected_Character";
    case Lexer_Status__Ok: return "Ok";
    case Lexer_Status__Token_Found: return "Token_Found";
    default: UNREACHABLE();
    }
}

const char *token_kind_name(Token_Kind kind)
{
    switch (kind) {
    case Token_Kind__None: return "None";

    case Token_Kind__Identifier: return "Identifier";
    case Token_Kind__Literal_Number: return "Literal_Number";
    case Token_Kind__Literal_Text: return "Literal_Text";

    case Token_Kind__Asterisk: return "Asterisk";
    case Token_Kind__Comma: return "Comma";
    case Token_Kind__Dot: return "Dot";
    case Token_Kind__Equals: return "Equals";
    case Token_Kind__Greater: return "Greater";
    case Token_Kind__Greater_Equals: return "Greater_Equals";
    case Token_Kind__Lesser: return "Lesser";
    case Token_Kind__Lesser_Equals: return "Lesser_Equals";
    case Token_Kind__Minus: return "Minus";
    case Token_Kind__Not_Equals: return "Not_Equals";
    case Token_Kind__Parenthesis_Close: return "Parenthesis_Close";
    case Token_Kind__Parenthesis_Open: return "Parenthesis_Open";
    case Token_Kind__Double_Pipe: return "Double_Pipe";
    case Token_Kind__Plus: return "Plus";
    case Token_Kind__Semicolon: return "Semicolon";
    case Token_Kind__Slash: return "Slash";

    default: UNREACHABLE();
    }
}

void print_token(const Token *token)
{
    const char *name = token_kind_name(token->kind);
    printf("%s", name);

    if (token->literal != NULL) printf("(%.*s)", (int)token->literal_length, token->literal);
}

void lexer_teardown(Lexer *lexer);

void lexer_setup(Lexer *lexer, const char *source, size_t source_length)
{
    lexer_teardown(lexer);
    lexer->begin = source;
    lexer->end = source + source_length;
    lexer->head = lexer->begin;
    lexer->line_start = lexer->begin;
    lexer->line = 0;
    lexer->tokens_used = 0;
    lexer->tokens_allocated = 64;
    lexer->tokens = malloc(lexer->tokens_allocated * sizeof lexer->tokens[0]);
    assert(lexer->tokens != NULL);
    lexer->strings = arena_create(64);
}

void lexer_teardown(Lexer *lexer)
{
    if (lexer == NULL) return;

    if (lexer->tokens != NULL) {
        free(lexer->tokens);
        lexer->tokens = NULL;
    }

    if (lexer->strings != NULL) arena_destroy(lexer->strings);
}

Token *lexer_next_token(Lexer *lexer, Token_Kind kind)
{
    if (lexer->next_token == NULL) {
        if (lexer->tokens_used >= lexer->tokens_allocated) {
            lexer->tokens_allocated = lexer->tokens_allocated == 0 ? 64 : lexer->tokens_allocated * 2;
            lexer->tokens = realloc(lexer->tokens, lexer->tokens_allocated * sizeof lexer->tokens[0]);
            assert(lexer->tokens != NULL);
        }
        
        lexer->next_token = &lexer->tokens[lexer->tokens_used++];
    }

    lexer->next_token->kind = kind;
    lexer->next_token->position = lexer->head - lexer->begin;
    lexer->next_token->line = lexer->line;
    lexer->next_token->offset = lexer->head - lexer->line_start;
    lexer->next_token->literal = NULL;
    lexer->next_token->literal_length = 0;

    return lexer->next_token;
}

void lexer_accept_token(Lexer *lexer)
{
    lexer->next_token = NULL;
}

void lexer_accept_next_token(Lexer *lexer, Token_Kind kind)
{
    lexer_next_token(lexer, kind);
    lexer_accept_token(lexer);
}

bool lexer_is_end(Lexer *lexer)
{
    return lexer->head >= lexer->end;
}

void lexer_skip_whitespace(Lexer *lexer)
{
    while (!lexer_is_end(lexer) && isspace(lexer->head[0])) ++lexer->head;
}

bool is_identifier_head(char rune)
{
    return isalpha(rune) || rune == '_';
}

bool is_identifier_tail(char rune)
{
    return isalnum(rune) || rune == '_';
}

Lexer_Status lexer_chop_string(Lexer *lexer, char delimiter)
{
    if (lexer->head[0] != delimiter) return Lexer_Status__Ok;
    
    bool delimiter_found = false;
    ++lexer->head;
    while (!lexer_is_end(lexer)) {
        if (delimiter_found) { /* Handle escaping. */
            delimiter_found = false;
            if (lexer->head[0] != delimiter) break;
        } else if (lexer->head[0] == delimiter) delimiter_found = true;

        ++lexer->head;
    }

    if (delimiter_found) return Lexer_Status__Invalid_String;
    return Lexer_Status__Token_Found;
}

Lexer_Status lexer_chop_simple_identifier(Lexer *lexer)
{
    if (!is_identifier_head(lexer->head[0])) return Lexer_Status__Ok;
    do ++lexer->head; while (!lexer_is_end(lexer) && is_identifier_tail(lexer->head[0]));
    return Lexer_Status__Token_Found;
}

Lexer_Status lexer_chop_quoted_identifier(Lexer *lexer)
{
    return lexer_chop_string(lexer, '"');
}

Lexer_Status lexer_tokenize_identifier(Lexer *lexer)
{
    Token *token = lexer_next_token(lexer, Token_Kind__Identifier);
    const char *literal = &lexer->begin[token->position];

    Lexer_Status status;
    if ((status = lexer_chop_simple_identifier(lexer)) != Lexer_Status__Ok) {
        if (status != Lexer_Status__Token_Found) return status;
        token->literal_length = lexer->head - literal;
    } else if ((status = lexer_chop_quoted_identifier(lexer)) != Lexer_Status__Ok) {
        if (status != Lexer_Status__Token_Found) return status;
        ++literal; /* Exclude opening quote from literal. */
        token->literal_length = lexer->head - literal - 1;
    } else return Lexer_Status__Ok;

    token->literal = arena_duplicate_string(lexer->strings, literal, token->literal_length);
    lexer_accept_token(lexer);
    return Lexer_Status__Token_Found;
}

Lexer_Status lexer_chop_literal_number(Lexer *lexer)
{
    if (!(isdigit(lexer->head[0]) || (lexer->head[0] == '.' && lexer->head != lexer->end && isdigit(lexer->head[1])))) return Lexer_Status__Ok;

    char *end;
    UNUSED(strtod(lexer->head, &end));

    if (lexer->head == end) return Lexer_Status__Invalid_Number;
    lexer->head = end;
    return Lexer_Status__Token_Found;
}

Lexer_Status lexer_chop_literal_text(Lexer *lexer)
{
    return lexer_chop_string(lexer, '\'');
}

Lexer_Status lexer_tokenize_literal(Lexer *lexer)
{
    Token *token = lexer_next_token(lexer, Token_Kind__None);
    const char *literal = &lexer->begin[token->position];

    Lexer_Status status;
    if ((status = lexer_chop_literal_number(lexer)) != Lexer_Status__Ok) {
        if (status != Lexer_Status__Token_Found) return status;
        token->kind = Token_Kind__Literal_Number;
        token->literal_length = lexer->head - literal;
    } else if ((status = lexer_chop_literal_text(lexer)) != Lexer_Status__Ok) {
        if (status != Lexer_Status__Token_Found) return status;
        token->kind = Token_Kind__Literal_Text;
        ++literal; /* Exclude opening quote from string. */
        token->literal_length = lexer->head - literal - 1;
    } else return Lexer_Status__Ok;

    token->literal = arena_duplicate_string(lexer->strings, literal, token->literal_length);
    lexer_accept_token(lexer);
    return Lexer_Status__Token_Found;
}

Lexer_Status lexer_tokenize_symbol(Lexer *lexer)
{
    Token_Kind kind = Token_Kind__None;

    switch (lexer->head[0]) {
    case '*': kind = Token_Kind__Asterisk; break;
    case ',': kind = Token_Kind__Comma; break;
    case '.': kind = Token_Kind__Dot; break;
    case '-': kind = Token_Kind__Minus; break;
    case '(': kind = Token_Kind__Parenthesis_Open; break;
    case ')': kind = Token_Kind__Parenthesis_Close; break;
    case '+': kind = Token_Kind__Plus; break;
    case ';': kind = Token_Kind__Semicolon; break;
    case '/': kind = Token_Kind__Slash; break;

    case '=': {
        kind = Token_Kind__Equals;
        if ((lexer->head + 1) != lexer->end && lexer->head[1] == '=') ++lexer->head;
    } break;

    case '>': {
        if ((lexer->head + 1) != lexer->end && lexer->head[1] == '=') {
            kind = Token_Kind__Greater_Equals;
            ++lexer->head;
        } else kind = Token_Kind__Greater;
    } break;

    case '<': {
        if ((lexer->head + 1) == lexer->end) {
            if (lexer->head[1] == '=') {
                kind = Token_Kind__Lesser_Equals;
                ++lexer->head;
            } else if (lexer->head[1] == '>') {
                kind = Token_Kind__Not_Equals;
                ++lexer->head;
            } else kind = Token_Kind__Lesser;
        } else kind = Token_Kind__Lesser;
    } break;

    case '!': {
        if (lexer->head + 1 == lexer->end || lexer->head[1] != '=') return Lexer_Status__Unexpected_Character;

        kind = Token_Kind__Not_Equals;
        ++lexer->head;
    } break;

    case '|': {
        if (lexer->head + 1 == lexer->end || lexer->head[1] != '|') return Lexer_Status__Unexpected_Character;

        kind = Token_Kind__Double_Pipe;
        ++lexer->head;
    } break;
    }

    if (kind != Token_Kind__None) {
        lexer_accept_next_token(lexer, kind);
        ++lexer->head;
        return Lexer_Status__Token_Found;
    }

    return Lexer_Status__Ok;
}

Lexer_Status lexer_skip_comment(Lexer *lexer)
{
    const char *head = lexer->head;

    /* Skip single line comments (starting with --): */
    if (head[0] == '-') {
        ++head;
        if (head == lexer->end || head[0] != '-') return Lexer_Status__Ok;

        do ++head; while (head < lexer->end && head[0] != '\n');
        lexer->head = head;
        if (head < lexer->end) ++lexer->head; /* Skip newline character. */

        return Lexer_Status__Token_Found;
    }

    /* Skip multiline comments (between / * and * /): */
    if (head[0] == '/') {
        ++head;
        if (head >= lexer->end || head[0] != '*') return Lexer_Status__Ok;

        do ++head; while (head < lexer->end && !(head[0] == '*' && (head + 1 != lexer->end && head[1] == '/')));
        if (head >= lexer->end) return Lexer_Status__Unclosed_Comment_Block;
        lexer->head = head + 2; /* skip comment block termination. */
        
        return Lexer_Status__Token_Found;
    }

    return Lexer_Status__Ok;
}

Lexer_Status lexer_tokenize_next(Lexer *lexer)
{
    Lexer_Status status;

    if ((status = lexer_tokenize_identifier(lexer)) != Lexer_Status__Ok) return status;
    if ((status = lexer_tokenize_literal(lexer)) != Lexer_Status__Ok) return status;
    if ((status = lexer_skip_comment(lexer)) != Lexer_Status__Ok) return status;
    if ((status = lexer_tokenize_symbol(lexer)) != Lexer_Status__Ok) return status;

    return Lexer_Status__Unexpected_Character;
}

Lexer_Status lexer_tokenize(Lexer *lexer)
{
    while (lexer->head < lexer->end) {
        lexer_skip_whitespace(lexer);

        if (lexer_is_end(lexer)) return Lexer_Status__Ok;

        Lexer_Status status = lexer_tokenize_next(lexer);
        if (status < Lexer_Status__Ok) return status;
    }

    return Lexer_Status__Ok;
}

int main(int argc, char **argv)
{
    UNUSED(argc, argv);

    const char *input =
        "-- List players victories and scores.\n"
        "SELECT\n"
        "    player.id AS \"Player ID\", 1000 420.69 .55435 .545aasd \n"
        "    player.nick_name AS \"Nickname\",\n"
        "    AGE(CURRENT_TIMESTAMP, player.created_at) AS \"\"\"Account\"\" age\",\n"
        "    SUM(match.score) AS \"Total Score\",\n"
        "    COUNT(CASE WHEN match.state = 'won' THEN 1 END) AS \"Victories\"\n"
        "FROM game.player\n"
        "LEFT JOIN game.player_match match\n"
        "    ON match.player_id = player.id\n"
        "WHERE player.status != 'inac''tive'\n"
        "    AND player.rank >= 2000\n"
        "    /*AND player.rank BETWEEN 2000 AND 3000*/\n"
        "    AND player.deleted_at IS NULL\n"
        "GROUP BY player.id\n";

    Lexer lexer = {0};
    lexer_setup(&lexer, input, strlen(input));

    Lexer_Status status = lexer_tokenize(&lexer);
    if (status != Lexer_Status__Ok) {
        printf("Failed to tokenize: %s\n", lexer_status_name(status));
        return EXIT_FAILURE;
    }

    printf("Tokens generated: x%zu\n", lexer.tokens_used);

    for (size_t i = 0; i < lexer.tokens_used; ++i) {
        printf("Token #%zu: ", i);
        print_token(&lexer.tokens[i]);
        printf("\n");
    }

    lexer_teardown(&lexer);
    return EXIT_SUCCESS;
}

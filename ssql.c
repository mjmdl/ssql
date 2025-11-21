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

    Token_Kind__All,
    Token_Kind__Alter,
    Token_Kind__And,
    Token_Kind__Any,
    Token_Kind__As,
    Token_Kind__Asc,
    Token_Kind__Avg,
    Token_Kind__Between,
    Token_Kind__By,
    Token_Kind__Case,
    Token_Kind__Check,
    Token_Kind__Constraint,
    Token_Kind__Count,
    Token_Kind__Create,
    Token_Kind__Current_Date,
    Token_Kind__Current_Time,
    Token_Kind__Current_Timestamp,
    Token_Kind__Default,
    Token_Kind__Delete,
    Token_Kind__Desc,
    Token_Kind__Distinct,
    Token_Kind__Drop,
    Token_Kind__Else,
    Token_Kind__End,
    Token_Kind__Exists,
    Token_Kind__Foreign,
    Token_Kind__From,
    Token_Kind__Full,
    Token_Kind__Group,
    Token_Kind__Having,
    Token_Kind__In,
    Token_Kind__Index,
    Token_Kind__Inner,
    Token_Kind__Insert,
    Token_Kind__Is,
    Token_Kind__Join,
    Token_Kind__Key,
    Token_Kind__Left,
    Token_Kind__Like,
    Token_Kind__Limit,
    Token_Kind__Max,
    Token_Kind__Min,
    Token_Kind__Not,
    Token_Kind__Null,
    Token_Kind__Offset,
    Token_Kind__On,
    Token_Kind__Or,
    Token_Kind__Order,
    Token_Kind__Outer,
    Token_Kind__Primary,
    Token_Kind__References,
    Token_Kind__Returning,
    Token_Kind__Right,
    Token_Kind__Select,
    Token_Kind__Sequence,
    Token_Kind__Sum,
    Token_Kind__Table,
    Token_Kind__Then,
    Token_Kind__Trigger,
    Token_Kind__Union,
    Token_Kind__Unique,
    Token_Kind__Update,
    Token_Kind__Values,
    Token_Kind__View,
    Token_Kind__When,
    Token_Kind__Where,

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

char to_lower_ascii(char character)
{
    const char offset = 'a' - 'A';
    if ('A' <= character && character <= 'Z') return character + offset;
    return character;
}

bool strings_equal_caseless(const char *first, const char *second, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (to_lower_ascii(first[i]) != to_lower_ascii(second[i])) return false;
    }
    return true;
}

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

    case Token_Kind__All: return "All";
    case Token_Kind__Alter: return "Alter";
    case Token_Kind__And: return "And";
    case Token_Kind__Any: return "Any";
    case Token_Kind__As: return "As";
    case Token_Kind__Asc: return "Asc";
    case Token_Kind__Avg: return "Avg";
    case Token_Kind__Between: return "Between";
    case Token_Kind__By: return "By";
    case Token_Kind__Case: return "Case";
    case Token_Kind__Check: return "Check";
    case Token_Kind__Constraint: return "Constraint";
    case Token_Kind__Count: return "Count";
    case Token_Kind__Create: return "Create";
    case Token_Kind__Current_Date: return "Current_Date";
    case Token_Kind__Current_Time: return "Current_Time";
    case Token_Kind__Current_Timestamp: return "Current_Timestamp";
    case Token_Kind__Default: return "Default";
    case Token_Kind__Delete: return "Delete";
    case Token_Kind__Desc: return "Desc";
    case Token_Kind__Distinct: return "Distinct";
    case Token_Kind__Drop: return "Drop";
    case Token_Kind__Else: return "Else";
    case Token_Kind__End: return "End";
    case Token_Kind__Exists: return "Exists";
    case Token_Kind__Foreign: return "Foreign";
    case Token_Kind__From: return "From";
    case Token_Kind__Full: return "Full";
    case Token_Kind__Group: return "Group";
    case Token_Kind__Having: return "Having";
    case Token_Kind__In: return "In";
    case Token_Kind__Index: return "Index";
    case Token_Kind__Inner: return "Inner";
    case Token_Kind__Insert: return "Insert";
    case Token_Kind__Is: return "Is";
    case Token_Kind__Join: return "Join";
    case Token_Kind__Key: return "Key";
    case Token_Kind__Left: return "Left";
    case Token_Kind__Like: return "Like";
    case Token_Kind__Limit: return "Limit";
    case Token_Kind__Max: return "Max";
    case Token_Kind__Min: return "Min";
    case Token_Kind__Not: return "Not";
    case Token_Kind__Null: return "Null";
    case Token_Kind__Offset: return "Offset";
    case Token_Kind__On: return "On";
    case Token_Kind__Or: return "Or";
    case Token_Kind__Order: return "Order";
    case Token_Kind__Outer: return "Outer";
    case Token_Kind__Primary: return "Primary";
    case Token_Kind__References: return "References";
    case Token_Kind__Returning: return "Returning";
    case Token_Kind__Right: return "Right";
    case Token_Kind__Select: return "Select";
    case Token_Kind__Sequence: return "Sequence";
    case Token_Kind__Sum: return "Sum";
    case Token_Kind__Table: return "Table";
    case Token_Kind__Then: return "Then";
    case Token_Kind__Trigger: return "Trigger";
    case Token_Kind__Union: return "Union";
    case Token_Kind__Unique: return "Unique";
    case Token_Kind__Update: return "Update";
    case Token_Kind__Values: return "Values";
    case Token_Kind__View: return "View";
    case Token_Kind__When: return "When";
    case Token_Kind__Where: return "Where";
        
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

Token_Kind lexer_test_keyword(const char *name, size_t length)
{
    switch (length) {
    case 2:
        if (strings_equal_caseless("as", name, 2)) return Token_Kind__As;
        if (strings_equal_caseless("by", name, 2)) return Token_Kind__By;
        if (strings_equal_caseless("in", name, 2)) return Token_Kind__In;
        if (strings_equal_caseless("is", name, 2)) return Token_Kind__Is;
        if (strings_equal_caseless("on", name, 2)) return Token_Kind__On;
        if (strings_equal_caseless("or", name, 2)) return Token_Kind__Or;
        break;
    case 3:
        if (strings_equal_caseless("all", name, 3)) return Token_Kind__All;
        if (strings_equal_caseless("and", name, 3)) return Token_Kind__And;
        if (strings_equal_caseless("any", name, 3)) return Token_Kind__Any;
        if (strings_equal_caseless("asc", name, 3)) return Token_Kind__Asc;
        if (strings_equal_caseless("avg", name, 3)) return Token_Kind__Avg;
        if (strings_equal_caseless("end", name, 3)) return Token_Kind__End;
        if (strings_equal_caseless("key", name, 3)) return Token_Kind__Key;
        if (strings_equal_caseless("max", name, 3)) return Token_Kind__Max;
        if (strings_equal_caseless("min", name, 3)) return Token_Kind__Min;
        if (strings_equal_caseless("not", name, 3)) return Token_Kind__Not;
        if (strings_equal_caseless("sum", name, 3)) return Token_Kind__Sum;
        break;
    case 4:
        if (strings_equal_caseless("case", name, 4)) return Token_Kind__Case;
        if (strings_equal_caseless("desc", name, 4)) return Token_Kind__Desc;
        if (strings_equal_caseless("drop", name, 4)) return Token_Kind__Drop;
        if (strings_equal_caseless("else", name, 4)) return Token_Kind__Else;
        if (strings_equal_caseless("from", name, 4)) return Token_Kind__From;
        if (strings_equal_caseless("full", name, 4)) return Token_Kind__Full;
        if (strings_equal_caseless("join", name, 4)) return Token_Kind__Join;
        if (strings_equal_caseless("left", name, 4)) return Token_Kind__Left;
        if (strings_equal_caseless("like", name, 4)) return Token_Kind__Like;
        if (strings_equal_caseless("null", name, 4)) return Token_Kind__Null;
        if (strings_equal_caseless("then", name, 4)) return Token_Kind__Then;
        if (strings_equal_caseless("view", name, 4)) return Token_Kind__View;
        if (strings_equal_caseless("when", name, 4)) return Token_Kind__When;
        break;
    case 5:
        if (strings_equal_caseless("alter", name, 5)) return Token_Kind__Alter;
        if (strings_equal_caseless("check", name, 5)) return Token_Kind__Check;
        if (strings_equal_caseless("count", name, 5)) return Token_Kind__Count;
        if (strings_equal_caseless("group", name, 5)) return Token_Kind__Group;
        if (strings_equal_caseless("index", name, 5)) return Token_Kind__Index;
        if (strings_equal_caseless("inner", name, 5)) return Token_Kind__Inner;
        if (strings_equal_caseless("limit", name, 5)) return Token_Kind__Limit;
        if (strings_equal_caseless("order", name, 5)) return Token_Kind__Order;
        if (strings_equal_caseless("outer", name, 5)) return Token_Kind__Outer;
        if (strings_equal_caseless("right", name, 5)) return Token_Kind__Right;
        if (strings_equal_caseless("table", name, 5)) return Token_Kind__Table;
        if (strings_equal_caseless("union", name, 5)) return Token_Kind__Union;
        if (strings_equal_caseless("where", name, 5)) return Token_Kind__Where;
        break;
    case 6:
        if (strings_equal_caseless("create", name, 6)) return Token_Kind__Create;
        if (strings_equal_caseless("delete", name, 6)) return Token_Kind__Delete;
        if (strings_equal_caseless("exists", name, 6)) return Token_Kind__Exists;
        if (strings_equal_caseless("having", name, 6)) return Token_Kind__Having;
        if (strings_equal_caseless("insert", name, 6)) return Token_Kind__Insert;
        if (strings_equal_caseless("offset", name, 6)) return Token_Kind__Offset;
        if (strings_equal_caseless("select", name, 6)) return Token_Kind__Select;
        if (strings_equal_caseless("unique", name, 6)) return Token_Kind__Unique;
        if (strings_equal_caseless("update", name, 6)) return Token_Kind__Update;
        if (strings_equal_caseless("values", name, 6)) return Token_Kind__Values;
        break;
    case 7:
        if (strings_equal_caseless("between", name, 7)) return Token_Kind__Between;
        if (strings_equal_caseless("default", name, 7)) return Token_Kind__Default;
        if (strings_equal_caseless("foreign", name, 7)) return Token_Kind__Foreign;
        if (strings_equal_caseless("primary", name, 7)) return Token_Kind__Primary;
        if (strings_equal_caseless("trigger", name, 7)) return Token_Kind__Trigger;
        break;
    case 8:
        if (strings_equal_caseless("distinct", name, 8)) return Token_Kind__Distinct;
        if (strings_equal_caseless("sequence", name, 8)) return Token_Kind__Sequence;
        break;
    case 9:
        if (strings_equal_caseless("returning", name, 9)) return Token_Kind__Returning;
        break;
    case 10:
        if (strings_equal_caseless("constraint", name, 10)) return Token_Kind__Constraint;
        if (strings_equal_caseless("references", name, 10)) return Token_Kind__References;
        break;
    case 11:
        break;
    case 12:
        if (strings_equal_caseless("current_date", name, 12)) return Token_Kind__Current_Date;
        if (strings_equal_caseless("current_time", name, 12)) return Token_Kind__Current_Time;
        break;
    case 17:
        if (strings_equal_caseless("current_timestamp", name, 17)) return Token_Kind__Current_Timestamp;
        break;
    }
    return Token_Kind__None;
}

Lexer_Status lexer_tokenize_keyword(Lexer *lexer, const char *name, size_t length)
{
    Token_Kind kind = lexer_test_keyword(name, length);
    if (kind == Token_Kind__None) return Lexer_Status__Ok;
    
    lexer_accept_next_token(lexer, kind);
    return Lexer_Status__Token_Found;
}

Lexer_Status lexer_tokenize_identifier(Lexer *lexer)
{
    const char *literal = lexer->head;
    size_t literal_length = 0;

    Lexer_Status status;
    if ((status = lexer_chop_simple_identifier(lexer)) != Lexer_Status__Ok) {
        if (status != Lexer_Status__Token_Found) return status;
        literal_length = lexer->head - literal;
        
        if ((status = lexer_tokenize_keyword(lexer, literal, literal_length)) != Lexer_Status__Ok) return status;
    } else if ((status = lexer_chop_quoted_identifier(lexer)) != Lexer_Status__Ok) {
        if (status != Lexer_Status__Token_Found) return status;
        ++literal; /* Exclude opening quote from literal. */
        literal_length = lexer->head - literal - 1;
    } else return Lexer_Status__Ok;

    Token *token = lexer_next_token(lexer, Token_Kind__Identifier);
    token->literal_length = literal_length;
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

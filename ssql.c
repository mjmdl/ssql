#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void unused() {}

#define UNUSED(...)     unused(__VA_ARGS__)
#define UNREACHABLE()   assert(false && "Unreachable!")
#define UNIMPLEMENTED() assert(false && "Unimplemented!")

typedef enum TokenKind {
    TokenKind_None = 0,

    TokenKind_Identifier,       /* table_name or "Table Name" */
    TokenKind_LiteralNumber,    /* 3.14 */
    TokenKind_LiteralText,      /* 'abc' */    
    
    TokenKind_Asterisk,         /* * */
    TokenKind_Comma,            /* , */
    TokenKind_Dot,              /* . */
    TokenKind_Equals,           /* = or == */
    TokenKind_Greater,          /* > */
    TokenKind_GreaterEquals,    /* >= */
    TokenKind_Lesser,           /* < */
    TokenKind_LesserEquals,     /* <= */
    TokenKind_Minus,            /* - */
    TokenKind_NotEquals,        /* <> or != */
    TokenKind_ParenthesisClose, /* ) */
    TokenKind_ParenthesisOpen,  /* ( */
    TokenKind_DoublePipe,       /* || */
    TokenKind_Plus,             /* + */
    TokenKind_Semicolon,        /* ; */
    TokenKind_Slash,            /* / */
} TokenKind;

typedef struct Token {
    TokenKind  kind;
    size_t     position;
    uint32_t   line;
    uint32_t   offset;
    char      *literal;
    size_t     literal_length;
} Token;

typedef struct Lexer {
    const char *begin;
    const char *end;
    const char *head;
    const char *line_start;
    size_t      line;
    Token      *tokens;
    size_t      tokens_used;
    size_t      tokens_allocated;
    Token      *next_token;
} Lexer;

typedef enum LexerStatus {
    LexerStatus_UnclosedString       = -5,
    LexerStatus_InvalidNumber        = -4,
    LexerStatus_InvalidString        = -3,
    LexerStatus_UnclosedCommentBlock = -2,
    LexerStatus_UnexpectedCharacter  = -1,
    LexerStatus_Ok                   = 0,
    LexerStatus_TokenFound           = 1,
} LexerStatus;

const char *lexer_status_name(LexerStatus status)
{
    switch (status) {
    case LexerStatus_UnclosedString:        return "UnclosedString";
    case LexerStatus_InvalidNumber:         return "InvalidNumber";
    case LexerStatus_InvalidString:         return "InvalidString";
    case LexerStatus_UnclosedCommentBlock:  return "UnclosedCommentBlock";
    case LexerStatus_UnexpectedCharacter:   return "UnexpectedCharacter";
    case LexerStatus_Ok:                    return "Ok";
    case LexerStatus_TokenFound:            return "TokenFound";
    default: UNREACHABLE();
    }
}

const char *token_kind_name(TokenKind kind)
{
    switch (kind) {
    case TokenKind_None:                return "None";
        
    case TokenKind_Identifier:          return "Identifier";
    case TokenKind_LiteralNumber:       return "LiteralNumber";
    case TokenKind_LiteralText:         return "LiteralText";
        
    case TokenKind_Asterisk:            return "Asterisk";
    case TokenKind_Comma:               return "Comma";
    case TokenKind_Dot:                 return "Dot";
    case TokenKind_Equals:              return "Equals";
    case TokenKind_Greater:             return "Greater";
    case TokenKind_GreaterEquals:       return "GreaterEquals";
    case TokenKind_Lesser:              return "Lesser";
    case TokenKind_LesserEquals:        return "LesserEquals";
    case TokenKind_Minus:               return "Minus";
    case TokenKind_NotEquals:           return "NotEquals";
    case TokenKind_ParenthesisClose:    return "ParenthesisClose";
    case TokenKind_ParenthesisOpen:     return "ParenthesisOpen";
    case TokenKind_DoublePipe:          return "DoublePipe";
    case TokenKind_Plus:                return "Plus";
    case TokenKind_Semicolon:           return "Semicolon";
    case TokenKind_Slash:               return "Slash";

    default:                            UNREACHABLE();
    }
}

void print_token(const Token *token)
{
    const char *name = token_kind_name(token->kind);
    printf("%s", name);
    
    if (token->literal != NULL)
        printf("(%.*s)", (int)token->literal_length, token->literal);
}

void lexer_teardown(Lexer *lexer);

void lexer_setup(Lexer *lexer, const char *source, size_t source_length)
{
    lexer_teardown(lexer);
    lexer->begin            = source;
    lexer->end              = source + source_length;
    lexer->head             = lexer->begin;
    lexer->line_start       = lexer->begin;
    lexer->line             = 0;
    lexer->tokens_used      = 0;
    lexer->tokens_allocated = 64;
    lexer->tokens           = malloc(lexer->tokens_allocated * sizeof lexer->tokens[0]);
    assert(lexer->tokens != NULL);
}

void lexer_teardown(Lexer *lexer)
{
    if (lexer == NULL)
        return;

    if (lexer->tokens != NULL) {
        for (size_t i = 0; i < lexer->tokens_used; ++i) {
            if (lexer->tokens[i].literal != NULL)
                free(lexer->tokens[i].literal);
        }
        
        free(lexer->tokens);
        lexer->tokens = NULL;
    }
}

Token *lexer_next_token(Lexer *lexer, TokenKind kind)
{
    if (lexer->next_token == NULL) {
        if (lexer->tokens_used >= lexer->tokens_allocated) {
            lexer->tokens_allocated = lexer->tokens_allocated == 0 ? 64 : lexer->tokens_allocated * 2;
            lexer->tokens           = realloc(lexer->tokens, lexer->tokens_allocated * sizeof lexer->tokens[0]);
            assert(lexer->tokens != NULL);
        }
        lexer->next_token = &lexer->tokens[lexer->tokens_used++];
    }
    
    lexer->next_token->kind           = kind;
    lexer->next_token->position       = lexer->head - lexer->begin;
    lexer->next_token->line           = lexer->line;
    lexer->next_token->offset         = lexer->head - lexer->line_start;
    lexer->next_token->literal        = NULL;
    lexer->next_token->literal_length = 0;
    
    return lexer->next_token;
}

void lexer_accept_token(Lexer *lexer)
{
    lexer->next_token = NULL;
}

bool lexer_is_end(Lexer *lexer)
{
    return lexer->head >= lexer->end;
}

void lexer_skip_whitespace(Lexer *lexer)
{
    while (!lexer_is_end(lexer) && isspace(lexer->head[0]))
        ++lexer->head;
}

bool lexer_is_double_quote(Lexer *lexer)
{
    return lexer->head[0] == '"' && (lexer->head == lexer->begin || lexer->head[-1] != '\\');
}

bool is_identifier_head(char rune)
{
    return isalpha(rune) || rune == '_';
}

bool is_identifier_tail(char rune)
{
    return isalnum(rune) || rune == '_';
}

char *duplicate_string(const char *from, size_t length)
{
    char *into = malloc(length + 1);
    assert(into != NULL);

    for (size_t i = 0; i < length; ++i)
        into[i] = from[i];

    into[length] = '\0';
    return into;
}

LexerStatus lexer_tokenize_identifier(Lexer *lexer)
{
    Token      *token   = lexer_next_token(lexer, TokenKind_Identifier);
    const char *literal = &lexer->begin[token->position];

    if (is_identifier_head(lexer->head[0])) {
        /* Tokenize simple identifiers (e.g. `table_name`): */
        do
            ++lexer->head;
        while (!lexer_is_end(lexer) && is_identifier_tail(lexer->head[0]));

        token->literal_length = lexer->head - literal;
    } else if (lexer_is_double_quote(lexer)) {
        /* Tokenize quoted identifiers (e.g. `"Table Name"`): */
        do
            ++lexer->head;
        while (!lexer_is_end(lexer) && !lexer_is_double_quote(lexer));
        
        ++literal;           /* Exclude opening quote from literal. */
        ++lexer->head;       /* Skip closing quote from source. */
        token->literal_length = lexer->head - literal - 1;
    } else
        /* Token is not a identifier. */
        return LexerStatus_Ok;
    
    token->literal = duplicate_string(literal, token->literal_length);
    lexer_accept_token(lexer);
    return LexerStatus_TokenFound;
}

LexerStatus lexer_tokenize_literal(Lexer *lexer)
{
    Token      *token   = lexer_next_token(lexer, TokenKind_None);
    const char *literal = &lexer->begin[token->position];
    
    if (isdigit(lexer->head[0]) || (lexer->head[0] == '.' && lexer->head != lexer->end && isdigit(lexer->head[1]))) {
        /* Tokenize numeric literal: */
        token->kind = TokenKind_LiteralNumber;
        
        char *end;
        UNUSED(strtod(lexer->head, &end));
        
        if (lexer->head == end)
            return LexerStatus_InvalidNumber;

        lexer->head = end;
        token->literal_length = lexer->head - literal;
    } else if (lexer->head[0] == '\'' && (lexer->head == lexer->begin || lexer->head[-1] != '\\')) {
        /* Tokenize text literal: */
        token->kind = TokenKind_LiteralText;

        do {
            ++lexer->head;
            
            if (lexer->head >= lexer->end)
                return LexerStatus_UnclosedString;
        } while (lexer->head[0] != '\'' || lexer->head[-1] == '\\');

        ++literal;              /* Exclude opening quote from string. */
        ++lexer->head;          /* Skip closing quote from source. */
        token->literal_length = lexer->head - literal - 1;
    } else
        /* Token is not a literal: */
        return LexerStatus_Ok;

    token->literal = duplicate_string(literal, token->literal_length);
    lexer_accept_token(lexer);
    return LexerStatus_TokenFound;
}

LexerStatus lexer_tokenize_symbol(Lexer *lexer)
{
    TokenKind kind = TokenKind_None;

    switch (lexer->head[0]) {
    case '*': kind = TokenKind_Asterisk;         break;
    case ',': kind = TokenKind_Comma;            break;
    case '.': kind = TokenKind_Dot;              break;
    case '-': kind = TokenKind_Minus;            break;
    case '(': kind = TokenKind_ParenthesisOpen;  break;
    case ')': kind = TokenKind_ParenthesisClose; break;
    case '+': kind = TokenKind_Plus;             break;
    case ';': kind = TokenKind_Semicolon;        break;
    case '/': kind = TokenKind_Slash;            break;

    case '=':
        kind = TokenKind_Equals;
        if ((lexer->head + 1) != lexer->end && lexer->head[1] == '=')
            ++lexer->head;
        break;

    case '>':
        if ((lexer->head + 1) != lexer->end && lexer->head[1] == '=') {
            kind = TokenKind_GreaterEquals;
            ++lexer->head;
        } else
            kind = TokenKind_Greater;
        break;

    case '<':
        if ((lexer->head + 1) == lexer->end) {
            if (lexer->head[1] == '=') {
                kind = TokenKind_LesserEquals;
                ++lexer->head;
            } else if (lexer->head[1] == '>') {
                kind = TokenKind_NotEquals;
                ++lexer->head;
            } else
                kind = TokenKind_Lesser;
        } else
            kind = TokenKind_Lesser;
        break;

    case '!':
        if (lexer->head + 1 == lexer->end || lexer->head[1] != '=')
            return LexerStatus_UnexpectedCharacter;

        kind = TokenKind_NotEquals;
        ++lexer->head;
        break;

    case '|':
        if (lexer->head + 1 == lexer->end || lexer->head[1] != '|')
            return LexerStatus_UnexpectedCharacter;
        
        kind = TokenKind_DoublePipe;
        ++lexer->head;
        break;
    }

    if (kind != TokenKind_None) {
        Token *token = lexer_next_token(lexer, kind);
        ++lexer->head;
        lexer_accept_token(lexer);
        return LexerStatus_TokenFound;
    }
    
    return LexerStatus_Ok;
}

LexerStatus lexer_skip_comment(Lexer *lexer)
{
    const char *head = lexer->head;

    /* Skip single line comments (starting with --): */
    if (head[0] == '-') {
        ++head;
        
        if (head == lexer->end || head[0] != '-')
            return LexerStatus_Ok;

        do
            ++head;
        while (head < lexer->end && head[0] != '\n');

        lexer->head = head;
        if (head < lexer->end)
            ++lexer->head;      /* Skip newline character. */

        return LexerStatus_TokenFound;
    }

    /* Skip multiline comments (between /* and * /): */
    if (head[0] == '/' && (head == lexer->begin || head[-1] != '\\')) {
        ++head;
        
        if (head >= lexer->end || head[0] != '*')
            return LexerStatus_Ok;
        
        do
            ++head;
        while (head < lexer->end && !(head[0] == '*' && (head + 1 != lexer->end && head[1] == '/')));

        if (head >= lexer->end) /* The comment is not closed! */
            return LexerStatus_UnclosedCommentBlock;

        lexer->head = head + 2; /* skip comment block termination. */

        return LexerStatus_TokenFound;
    }

    return LexerStatus_Ok;
}

LexerStatus lexer_tokenize_next(Lexer *lexer)
{
    LexerStatus status;
    
    if ((status = lexer_tokenize_identifier(lexer)) != LexerStatus_Ok)
        return status;

    if ((status = lexer_tokenize_literal(lexer)) != LexerStatus_Ok)
        return status;

    if ((status = lexer_skip_comment(lexer)) != LexerStatus_Ok)
        return status;

    if ((status = lexer_tokenize_symbol(lexer)) != LexerStatus_Ok)
        return status;

    return LexerStatus_UnexpectedCharacter;
}

LexerStatus lexer_tokenize(Lexer *lexer)
{
    while (lexer->head < lexer->end) {
        lexer_skip_whitespace(lexer);

        if (lexer_is_end(lexer))
            return LexerStatus_Ok;

        LexerStatus status = lexer_tokenize_next(lexer);
        if (status < LexerStatus_Ok)
            return status;
    }
    
    return LexerStatus_Ok;
}

int main(int argc, char **argv)
{
    UNUSED(argc, argv);

    const char *input =
        "-- List players victories and scores.                               \n"
        "SELECT                                                              \n"
        "    player.id        AS \"Player ID\",                              \n"
        "    player.nick_name AS \"Nickname\",                               \n"
        "    AGE(CURRENT_TIMESTAMP, player.created_at) AS \"Account age\",   \n"
        "    SUM(match.score) AS \"Total Score\",                            \n"
        "    COUNT(CASE WHEN match.state = 'won' THEN 1 END) AS \"Victories\"\n"
        "FROM game.player                                                    \n"
        "LEFT JOIN game.player_match match                                   \n"
        "    ON match.player_id = player.id                                  \n"
        "WHERE player.status != 'inactive'                                   \n"
        "    AND player.rank >= 2000                                         \n"
        "    /*AND player.rank BETWEEN 2000 AND 3000*/                       \n"
        "    AND player.deleted_at IS NULL                                   \n"
        "GROUP BY player.id                                                  \n";
    
    Lexer lexer = {0};
    lexer_setup(&lexer, input, strlen(input));

    LexerStatus status = lexer_tokenize(&lexer);
    if (status != LexerStatus_Ok) {
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

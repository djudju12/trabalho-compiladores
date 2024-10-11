#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define ASSERT assert

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

enum Token_Kind {
    TOKEN_PROCESS = 0,
    TOKEN_EVENTS,
    TOKEN_END,
    TOKEN_ID,
    TOKEN_TYPE,
    TOKEN_OPP,
    TOKEN_CLP,
    TOKEN_EOE,
    TOKEN_STR,
    TOKEN_ATR,
    TOKEN_NEXT,
    TOKEN_EOF,
    __TOKENS_COUNT
};

char *TOKEN_DESC[] = {
    [TOKEN_PROCESS] = "Token Process",
    [TOKEN_EVENTS]  = "Token Events",
    [TOKEN_END]     = "Token End",
    [TOKEN_TYPE]    = "Token Type",
    [TOKEN_ID]      = "Token Identifier",
    [TOKEN_CLP]     = "Token Close Parenteses",
    [TOKEN_OPP]     = "Token Open Parenteses",
    [TOKEN_EOE]     = "Token End of Expression",
    [TOKEN_STR]     = "Token String",
    [TOKEN_ATR]     = "Token Assignment",
    [TOKEN_NEXT]    = "Token Next",
    [TOKEN_EOF]     = "Token End of File"
};

_Static_assert(ARRAY_SIZE(TOKEN_DESC) == __TOKENS_COUNT, "Make sure that you have implemented description for new tokens! :)");

#define MAX_TOKEN_LEN 256

typedef struct {
    char value[MAX_TOKEN_LEN];
    enum Token_Kind kind;
} Token;

typedef struct {
    char *content;
    size_t col;
    size_t row;
    const char *file_path;
} Lexer;

typedef struct Keyword_Node Keyword_Node;

struct Keyword_Node {
    char *key;
    size_t key_len;
    Token token;
    Keyword_Node *next;
};

typedef struct {
    Keyword_Node *head;
    Keyword_Node *current;
} Keyword_Table;

Keyword_Node *get_keyword(Keyword_Table *table, char *key, size_t key_len) {
    if (key_len == table->head->key_len && memcmp(table->head->key, key, key_len) == 0) {
        return table->head;
    }

    table->current = table->head;
    while (table->current->next != NULL) {
        if (key_len == table->current->next->key_len && memcmp(table->current->next->key, key, key_len) == 0) {
            Keyword_Node *new_head = table->current->next;
            table->current->next = table->current->next->next;
            new_head->next = table->head;
            table->head = new_head;
            return new_head;
        }

        table->current = table->current->next;
    }

    return NULL;
}

void set_keyword(Keyword_Table *table, Keyword_Node *keyword) {
    if (table->head == NULL) {
        table->head = keyword;
        return;
    }

    table->current = table->head;
    while (table->current->next != NULL) {
        table->current = table->current->next;
    }

    table->current->next = keyword;
}


static Keyword_Table keyword_table = {0};
#define NEW_KEYWORD(_key, token_kind) \
{ \
    .key = (_key), \
    .key_len = ARRAY_SIZE((_key)) - 1, \
    .token = { \
        .kind = (token_kind), \
        .value = (_key) \
    } \
}

static Keyword_Node keywords[] = {
    NEW_KEYWORD("PROCESS", TOKEN_PROCESS),
    NEW_KEYWORD("EVENTS", TOKEN_EVENTS),
    NEW_KEYWORD("END", TOKEN_END),
    NEW_KEYWORD("TASK", TOKEN_TYPE),
    NEW_KEYWORD("STARTER", TOKEN_TYPE),
};

static void build_keyword_table() {
    for (size_t i = 0; i < ARRAY_SIZE(keywords); i++) {
        set_keyword(&keyword_table, &keywords[i]);
    }
}

char *read_file(const char *file_path) {
    char *content = NULL;
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        printf("Cannot open file %s", file_path);
        goto CLEAN_UP;
    }

    if (fseek(file, 0L, SEEK_END) != 0) goto CLEAN_UP;

    long file_size = ftell(file);
    content = malloc(sizeof(char) * file_size);
    rewind(file);

    long bytes_readed = fread(content, sizeof(char), file_size, file);
    if (bytes_readed != file_size) {
        printf("Cannot read full file\n");
        printf("File size: %ld\n", file_size);
        printf("Bytes readed: %ld\n", bytes_readed);
        goto CLEAN_UP;
    }

    return content;

CLEAN_UP:
    if (file) {
        fclose(file);
    }

    if (content) {
        free(content);
    }

    return NULL;
}

char lex_getc(Lexer *lexer) {
    char c = *lexer->content;
    if (c == '\0') return '\0';

    if (c == '\n') {
        lexer->col = 1;
        lexer->row += 1;
    } else {
        lexer->col += 1;
    }

    lexer->content++;
    return c;
}

char lex_peekc(Lexer *lexer) {
    return *lexer->content;
}

char lex_trim_left(Lexer *lexer) {
    char c = lex_getc(lexer);
    while (c != '\0' && isspace(c)) {
        c = lex_getc(lexer);
    }

    return c;
}

#define PRINT_ERROR(lexer, msg) fprintf(stderr, "%s:%ld:%ld: error: "msg"\n", (lexer)->file_path, (lexer)->row, (lexer)->col);
#define PRINT_ERROR_FMT(lexer, format, ...) fprintf(stderr, "%s:%ld:%ld: error: "format"\n", (lexer)->file_path, (lexer)->row, (lexer)->col, __VA_ARGS__);

Token next_token(Lexer *lexer) {
    Token token = {0};

    char c = lex_trim_left(lexer);

    if (c == '\0') {
        token.kind = TOKEN_EOF;
        return token;
    }

    size_t len = 0;
    token.value[len++] = c;
    switch (c) {
        case '(': token.kind = TOKEN_OPP; break;
        case ')': token.kind = TOKEN_CLP; break;
        case ';': token.kind = TOKEN_EOE; break;
        case '=': token.kind = TOKEN_ATR; break;

        case '-': {
            c = lex_getc(lexer);
            if (c != '>') {
                PRINT_ERROR_FMT(lexer, "Expected `>`, find `%c`", c);
                exit(EXIT_FAILURE);
            }

            token.value[len++] = c;
            token.kind = TOKEN_NEXT;
        } break;


        case '\'': {
            len = 0;
            c = lex_getc(lexer);
            while (len < (MAX_TOKEN_LEN - 1) && c != '\0' && c != '\'' && c != '\n') {
                token.value[len++] = c;
                c = lex_getc(lexer);
            }

            if (c != '\'') {
                PRINT_ERROR(lexer, "Unexpected end of string literal");
                exit(EXIT_FAILURE);
            }

            token.kind = TOKEN_STR;
        } break;

        default: {
            if (!isalpha(c)) {
                PRINT_ERROR_FMT(lexer, "Invalid character `%c`", c);
                exit(EXIT_FAILURE);
            }

            char peek = lex_peekc(lexer);
            while (
                len < (MAX_TOKEN_LEN - 1) &&
                c != '\0' &&
                (isalnum(peek) || peek == '_')
            ) {
                c = lex_getc(lexer);
                token.value[len++] = c;
                peek = lex_peekc(lexer);
            }

            token.value[len] = '\0';
            Keyword_Node *keyword = get_keyword(&keyword_table, token.value, len);
            if (keyword != NULL) {
                return keyword->token;
            }

            token.kind = TOKEN_ID;
        }
    }

    token.value[len] = '\0';
    return token;
}

static char* shift_args(int *argc, char ***argv) {
    ASSERT(*argc > 0 && "Shifting empty command line arguments!");
    (*argc)--;
    return *(*argv)++;
}

static void usage(char *program_name) {
    printf("Usage: %s <FILE>\n", program_name);
}

int main(int argc, char **argv) {
    char *program_name = shift_args(&argc, &argv);
    if (argc == 0) {
        usage(program_name);
        exit(1);
    }

    char *file_path = shift_args(&argc, &argv);

    build_keyword_table();
    Lexer lexer = {0};
    lexer.content = read_file(file_path);
    lexer.file_path = file_path;
    lexer.col = 1;
    lexer.row = 1;

    Token token = {0};
    do {
        token = next_token(&lexer);
        printf("(\"%s\", %s)\n", token.value, TOKEN_DESC[token.kind]);
    } while (token.kind != TOKEN_EOF);
    return 0;
}
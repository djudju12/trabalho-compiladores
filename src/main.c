#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include <raylib.h>
#include <raymath.h>

#define ASSERT assert
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

char* shift_args(int *argc, char ***argv) {
    ASSERT(*argc > 0 && "Shifting empty command line arguments!");
    (*argc)--;
    return *(*argv)++;
}

void usage(char *program_name) {
    printf("Usage: %s <FILE>\n", program_name);
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


/*******************************************************************\
| Section: Symbols Table                                            |
| A simple and static implementations of a hash table that uses     |
| linear probing has a collision resolution strategy                |
\*******************************************************************/

#define MAX_TOKEN_LEN 256
#define HASHMAP_CAPACITY 16384
#define HASHMAP_INDEX(h) (h & (HASHMAP_CAPACITY - 1))
#define MAX_SYMBOLS

typedef enum {
    EVENT_STARTER = 0,
    EVENT_TASK
} Event_Kind;

typedef enum {
    EVENT = 0
} Symb_Kind;

typedef struct {
    Symb_Kind kind;
    union {
        struct Event_Symb {
            Event_Kind kind;
            char title[MAX_TOKEN_LEN];
        } event;
    } as;
    int obj_id;
} Symbol;

typedef struct {
    char key[MAX_TOKEN_LEN];
    Symbol value;
    bool occupied;
} Key_Value;

typedef struct {
    Key_Value entries[HASHMAP_CAPACITY];
    size_t len;
} Hash_Map;

size_t hash(char *key, size_t *key_len) {
    size_t h = 0;
    size_t i = 0;
    for (; key[i] != '\0' ; i++) {
        h = ((h << 5) - h) + (unsigned char)key[i];
    }

    if (key_len) {
        *key_len = i;
    }

    return h;
}

Key_Value *hm_get(Hash_Map *map, char *key) {
    size_t key_len = 0;
    size_t h = hash(key, &key_len);

    Key_Value *entry = &map->entries[HASHMAP_INDEX(h)];
    while (entry->occupied && memcmp(entry->key, key, key_len) != 0) {
        h++;
        entry = &map->entries[HASHMAP_INDEX(h)];
    }

    return entry->occupied? entry : NULL;
}

void hm_put(Hash_Map *map, char *key, Symbol symbol) {
    size_t key_len = 0;
    size_t h = hash(key, &key_len);
    Key_Value *entry = &map->entries[HASHMAP_INDEX(h)];

    ASSERT(map->len < HASHMAP_CAPACITY && "Symbols Table is full!");

    while (entry->occupied && memcmp(entry->key, key, key_len) != 0) {
        h++;
        entry = &map->entries[HASHMAP_INDEX(h)];
    }

    if (!entry->occupied) {
        map->len++;
        memcpy(entry->key, key, key_len);
        entry->occupied = true;
        entry->value = symbol;
        entry->value.obj_id = -1;
    } else {
        // TODO: update symbol. Will be needed soon
    }

}

/*******************************************************************\
| Section: Tokens definition                                        |
\*******************************************************************/

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
    [TOKEN_PROCESS] = "PROCESS",
    [TOKEN_EVENTS]  = "EVENTS",
    [TOKEN_END]     = "END",
    [TOKEN_TYPE]    = "TYPE",
    [TOKEN_ID]      = "ID",
    [TOKEN_CLP]     = "CLOSE_PAREN",
    [TOKEN_OPP]     = "OPEN_PAREN",
    [TOKEN_EOE]     = "EOE",
    [TOKEN_STR]     = "STRING",
    [TOKEN_ATR]     = "ASSIGNMENT",
    [TOKEN_NEXT]    = "NEXT",
    [TOKEN_EOF]     = "EOF"
};

_Static_assert(ARRAY_SIZE(TOKEN_DESC) == __TOKENS_COUNT, "Make sure that you have implemented description for new tokens! :)");

typedef struct {
    char value[MAX_TOKEN_LEN];
    enum Token_Kind kind;
} Token;

/*******************************************************************\
| Section: Key Words Table                                          |
| This is a kind of self-organizing linked list where the last      |
| used keyword is put in the top node of the list. More information |
| about this structure can be found in the book:                    |
| Algorithms + Data Structures = Programs - Niklaus Wirth           |
\*******************************************************************/

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

Keyword_Table keyword_table = {0};

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

#define NEW_KEYWORD(_key, token_kind) \
{ \
    .key = (_key), \
    .key_len = ARRAY_SIZE((_key)) - 1, \
    .token = { \
        .kind = (token_kind), \
        .value = (_key) \
    } \
}

Keyword_Node keywords[] = {
    NEW_KEYWORD("PROCESS", TOKEN_PROCESS),
    NEW_KEYWORD("EVENTS", TOKEN_EVENTS),
    NEW_KEYWORD("END", TOKEN_END),
    NEW_KEYWORD("TASK", TOKEN_TYPE),
    NEW_KEYWORD("STARTER", TOKEN_TYPE),
};

void build_keyword_table() {
    for (size_t i = 0; i < ARRAY_SIZE(keywords); i++) {
        set_keyword(&keyword_table, &keywords[i]);
    }
}

/*******************************************************************\
| Section: Lexer                                                    |
| TODO: Write somehting about the implementation of the lexer       |
\*******************************************************************/

#define PRINT_ERROR(lexer, msg) fprintf(stderr, "%s:%ld:%ld: error: "msg"\n", (lexer)->file_path, (lexer)->row, (lexer)->col);
#define PRINT_ERROR_FMT(lexer, format, ...) fprintf(stderr, "%s:%ld:%ld: error: "format"\n", (lexer)->file_path, (lexer)->row, (lexer)->col, __VA_ARGS__);

typedef struct {
    char *content;
    size_t col, row;
    const char *file_path;
    Hash_Map symbols;
    Token token;
} Lexer;

void init_lexer(Lexer *lexer, const char *file_path) {
    lexer->col = 0;
    lexer->row = 0;
    lexer->file_path = file_path;
    lexer->content = read_file(file_path);
    lexer->symbols.len = 0;
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

Token next_token(Lexer *lexer) {
    char c = lex_trim_left(lexer);
    size_t len = 0;
    lexer->token.value[len++] = c;

    if (c == '\0') {
        lexer->token.kind = TOKEN_EOF;
        return lexer->token;
    }

    switch (c) {
        case '(': lexer->token.kind = TOKEN_OPP; break;
        case ')': lexer->token.kind = TOKEN_CLP; break;
        case ';': lexer->token.kind = TOKEN_EOE; break;
        case '=': lexer->token.kind = TOKEN_ATR; break;

        case '-': {
            c = lex_getc(lexer);
            if (c != '>') {
                PRINT_ERROR_FMT(lexer, "Expected `>`, find `%c`", c);
                exit(EXIT_FAILURE);
            }

            lexer->token.value[len++] = c;
            lexer->token.kind = TOKEN_NEXT;
        } break;


        case '\'': {
            len = 0;
            c = lex_getc(lexer);
            while (len < (MAX_TOKEN_LEN - 1) && c != '\0' && c != '\'' && c != '\n') {
                lexer->token.value[len++] = c;
                c = lex_getc(lexer);
            }

            if (c != '\'') {
                PRINT_ERROR(lexer, "Unexpected end of string literal");
                exit(EXIT_FAILURE);
            }

            lexer->token.kind = TOKEN_STR;
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
                lexer->token.value[len++] = c;
                peek = lex_peekc(lexer);
            }

            lexer->token.value[len] = '\0';
            Keyword_Node *keyword = get_keyword(&keyword_table, lexer->token.value, len);
            if (keyword != NULL) {
                lexer->token.kind = keyword->token.kind;
            } else {
                lexer->token.kind = TOKEN_ID;
            }
        }
    }

    lexer->token.value[len] = '\0';
    printf("<`%s`, %s>\n", lexer->token.value, TOKEN_DESC[lexer->token.kind]);
    return lexer->token;
}

Token next_token_fail_if_eof(Lexer *lexer) {
    next_token(lexer);
    if (lexer->token.kind == TOKEN_EOF) {
        PRINT_ERROR(lexer, "Unexpected end of file");
        exit(EXIT_FAILURE);
    }

    return lexer->token;
}

/*******************************************************************\
| Section: Visualization of BPMN                                    |
| This Program is using Raylib has a graphic library.               |
| Raylib aims to be simple, providing useful functions and          |
| primitives.                                                       |
| More can be found in the following repository:                    |
| https://github.com/raysan5/raylib/                                |
\*******************************************************************/

typedef struct {
    Rectangle rect;
    Symbol *value;
    int points_to;
    bool ploted;
} Screen_Object;

#define MAX_SCREEN_OBJECTS 512

typedef struct {
    Screen_Object screen_objects[MAX_SCREEN_OBJECTS];
    size_t objs_cnt;
    char title[MAX_TOKEN_LEN];
    int height, width;
    int cols, rows;
} Screen;

void init_screen(Screen *screen) {
    screen->rows = 1;
    screen->cols = 10;
    screen->height = screen->rows * 400;
    screen->width = screen->cols * 150;
    screen->objs_cnt = 0;
    memcpy(screen->title, "Default Title", 14);
}

size_t push_obj(Screen *screen, Screen_Object obj) {
    ASSERT(screen->objs_cnt < MAX_SCREEN_OBJECTS && "TODO: maybe use a dynamic array?");
    screen->screen_objects[screen->objs_cnt] = obj;
    return screen->objs_cnt++;
}

#define RECT_POS(rect) (Vector2) { .x = (rect).x, .y = (rect).y }
Vector2 grid2world(Screen screen, Vector2 grid_pos, int obj_size) {
    Vector2 units = { screen.width / screen.cols, screen.height / screen.rows };
    return (Vector2) { .x = grid_pos.x * units.x, grid_pos.y * units.y + (screen.height/2 - obj_size/2) };
}

void draw_arrow(Screen screen, Screen_Object from, Screen_Object to) {
    Vector2 world_from = grid2world(screen, RECT_POS(from.rect), from.rect.width);

    Vector2 v2_from = {
        .x = world_from.x + from.rect.width,
        .y = world_from.y + from.rect.height/2
    };

    Vector2 world_to = grid2world(screen, RECT_POS(to.rect), to.rect.width);
    Vector2 v2_to = {
        .x = world_to.x,
        .y = world_to.y + to.rect.height/2
    };

    DrawLineV(v2_from, v2_to, BLACK);
    Vector2 v2_point = v2_to;
    v2_point.x -= 4;
    DrawCircleV(v2_point, 4, BLACK);
}

int measure_text_lines(Rectangle rect, const char **words, int word_count, int font_size) {
    int space_left = rect.width;
    Vector2 pos = { .x = rect.x, .y = rect.y };
    int total_lines = 1;
    for (int i = 0; i < word_count; i++) {
        int word_len = MeasureText(words[i], font_size) + font_size;
        if (word_len > space_left) {
            space_left = rect.width - word_len;
            pos.y += font_size;
            pos.x = rect.x;
            total_lines++;
        } else {
            space_left -= word_len;
        }

        pos.x += word_len;
    }

    return total_lines;
}

void draw_fitting_text(Rectangle rect, char *text, int font_size, int margin) {
    int word_count = 0;
    const char **words = TextSplit(text, ' ', &word_count);
    int space_left = rect.width;
    int total_lines = measure_text_lines(rect, words, word_count, font_size);
    if ((total_lines * font_size) > rect.height) {
        font_size = rect.height / total_lines;
    }

    rect.x += margin;
    rect.y += margin;
    rect.width -= margin*2;
    rect.height -= margin*2;
    Vector2 pos = { .x = rect.x, .y = rect.y };
    for (int i = 0; i < word_count; i++) {
        int word_len = MeasureText(words[i], font_size) + font_size;

        if (word_len > space_left) {
            space_left = rect.width - word_len;
            pos.y += font_size;
            pos.x = rect.x;
        } else {
            space_left -= word_len;
        }

        DrawText(words[i], pos.x, pos.y, font_size, BLACK);
        pos.x += word_len;
    }
}

void draw_obj(Screen screen, Screen_Object obj) {
    Vector2 world_obj_pos = grid2world(screen, RECT_POS(obj.rect), obj.rect.width);
    Rectangle world_obj_rect = {
        .x = world_obj_pos.x,
        .y = world_obj_pos.y,
        .width = obj.rect.width,
        .height = obj.rect.height
    };

    ASSERT(world_obj_rect.x < screen.cols*screen.width && world_obj_rect.y < screen.rows*screen.height && "Object out of bounds");

    if (obj.value->kind == EVENT) {
        switch (obj.value->as.event.kind) {

            case EVENT_STARTER: {
                Vector2 pos = {world_obj_rect.x, world_obj_rect.y};
                pos.x += world_obj_rect.width/2;
                pos.y += world_obj_rect.height/2;
                DrawCircleV(pos, world_obj_rect.width/2, GREEN);
            } break;

            case EVENT_TASK: {
                DrawRectangleRoundedLines(world_obj_rect, 0.3f, 0, 1, BLACK);
                draw_fitting_text(world_obj_rect, obj.value->as.event.title, 15, 5);
            } break;

            default: ASSERT(0 && "Unreachable statement");
        }

        if (obj.points_to > 0) {
            draw_arrow(screen, obj, screen.screen_objects[obj.points_to]);
        }
    }
}

/*******************************************************************\
| Section: Parser                                                   |
| TODO: Write somehting about the implementation of the Parser      |
\*******************************************************************/

void parse(Lexer *lexer, Screen *screen) {
    void parse_events(Lexer *lexer, Screen *screen);
    void parse_process(Lexer *lexer);

    next_token(lexer);
    if (lexer->token.kind != TOKEN_PROCESS) {
        PRINT_ERROR_FMT(lexer, "Expected new `PROCESS`, find `%s`", lexer->token.value);
        exit(EXIT_FAILURE);
    }

    next_token_fail_if_eof(lexer);
    if (lexer->token.kind != TOKEN_STR) {
        PRINT_ERROR_FMT(lexer, "Expected name of the process, find `%s`", lexer->token.value);
        exit(EXIT_FAILURE);
    }

    memcpy(screen->title, lexer->token.value, MAX_TOKEN_LEN);

    next_token(lexer);
    if (lexer->token.kind == TOKEN_EVENTS) {
        parse_events(lexer, screen);
    }

    int col = 0;
    Symbol *last = NULL;
    while (lexer->token.kind != TOKEN_END) {
        if (lexer->token.kind == TOKEN_NEXT) {
            next_token_fail_if_eof(lexer);
            if (lexer->token.kind != TOKEN_ID) {
                PRINT_ERROR_FMT(lexer, "Expected identifier, found %s", lexer->token.value);
                exit(EXIT_FAILURE);
            }

            Key_Value *kv = hm_get(&lexer->symbols, lexer->token.value);
            if (kv == NULL) {
                PRINT_ERROR_FMT(lexer, "Identifier `%s` does not exists", lexer->token.value);
                exit(EXIT_FAILURE);
            }

            if (kv->value.kind != EVENT) {
                PRINT_ERROR(lexer, "Invalid identifier, expected `Event`");
                exit(EXIT_FAILURE);
            }

            Screen_Object *obj = &screen->screen_objects[kv->value.obj_id];
            if (!obj->ploted) {
                obj->rect.x = col++;
                obj->ploted = true;
            }

            if (last != NULL && kv->value.obj_id >= 0) {
                screen->screen_objects[last->obj_id].points_to = kv->value.obj_id;
            }

            last = &kv->value;
            next_token(lexer);
        }
    }
}

void parse_process(Lexer *lexer) {
    Token process_name = next_token(lexer);
    if (process_name.kind != TOKEN_ID) {
        PRINT_ERROR_FMT(lexer, "Expected process name, find `%s`", process_name.value);
        exit(EXIT_FAILURE);
    }
}

void parse_events(Lexer *lexer, Screen *screen) {
    void parse_single_event(Lexer *lexer, Screen *screen);
    ASSERT(lexer->token.kind == TOKEN_EVENTS && "Unexpected token");

    next_token_fail_if_eof(lexer);
    while (lexer->token.kind != TOKEN_END) {
        parse_single_event(lexer, screen);
    }

    next_token_fail_if_eof(lexer);
}

void parse_single_event(Lexer *lexer, Screen *screen) {
    if (lexer->token.kind == TOKEN_ID) {
        hm_put(&lexer->symbols, lexer->token.value, (Symbol){ .kind = EVENT, .obj_id = -1 });
        Key_Value *kv = hm_get(&lexer->symbols, lexer->token.value);

        next_token_fail_if_eof(lexer);
        if (lexer->token.kind != TOKEN_ATR) {
            PRINT_ERROR_FMT(lexer, "Expected `=`, find %s", lexer->token.value);
            exit(EXIT_FAILURE);
        }

        next_token_fail_if_eof(lexer);
        if (lexer->token.kind != TOKEN_TYPE) {
            PRINT_ERROR_FMT(lexer, "Expected type definition, find %s", lexer->token.value);
            exit(EXIT_FAILURE);
        }

        if (strcmp(lexer->token.value, "STARTER") == 0) {
            kv->value.as.event.kind = EVENT_STARTER;
        } else if (strcmp(lexer->token.value, "TASK") == 0) {
            kv->value.as.event.kind = EVENT_TASK;
        } else {
            PRINT_ERROR(lexer, "Invalid type");
            exit(EXIT_FAILURE);
        }

        next_token_fail_if_eof(lexer);
        if (lexer->token.kind != TOKEN_OPP) {
            PRINT_ERROR(lexer, "Syntax error");
            exit(EXIT_FAILURE);
        }

        Screen_Object obj = {
            .rect = { .height = 100, .width = 100, .x = 0, .y = 0 },
            .value = &kv->value
        };

        if (kv->value.as.event.kind == EVENT_TASK) {
            next_token_fail_if_eof(lexer);
            if (lexer->token.kind == TOKEN_STR) {
                memcpy(kv->value.as.event.title, lexer->token.value, MAX_TOKEN_LEN);
            }
            kv->value.obj_id = push_obj(screen, obj);
        } else if (kv->value.as.event.kind == EVENT_STARTER) {
            obj.rect.height *= 0.5;
            obj.rect.width *= 0.5;
            kv->value.obj_id = push_obj(screen, obj);
        }

        next_token_fail_if_eof(lexer);
        if (lexer->token.kind != TOKEN_CLP) {
            PRINT_ERROR(lexer, "Syntax error");
            exit(EXIT_FAILURE);
        }

        next_token_fail_if_eof(lexer);
    } else {
        PRINT_ERROR_FMT(lexer, "Expected event declaration, find %s", lexer->token.value); // TODO: do a better job handling this error
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv) {
    char *program_name = shift_args(&argc, &argv);
    if (argc == 0) {
        usage(program_name);
        return EXIT_FAILURE;
    }

    char *file_path = shift_args(&argc, &argv);
    static Lexer lexer = {0};
    static Screen screen = {0};

    init_lexer(&lexer, file_path);
    init_screen(&screen);
    build_keyword_table();

    parse(&lexer, &screen);

    InitWindow(screen.width, screen.height, screen.title);
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(WHITE);

        for (size_t i = 0; i < screen.objs_cnt; i++) {
            draw_obj(screen, screen.screen_objects[i]);
        }

        EndDrawing();
    }

    CloseWindow();

    return EXIT_SUCCESS;
}
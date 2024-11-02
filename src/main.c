#include <assert.h>
#include <ctype.h>
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT assert
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

char *shift_args(int *argc, char ***argv) {
    ASSERT(*argc > 0 && "Shifting empty command line arguments!");
    (*argc)--;
    return *(*argv)++;
}

void usage(char *program_name) { printf("Usage: %s <FILE>\n", program_name); }

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

typedef enum { EVENT_STARTER = 0, EVENT_TASK, EVENT_INVALID } Event_Kind;

typedef enum { SYMB_EVENT = 0, SYMB_SUBPROCESS } Symb_Kind;

typedef struct {
    Symb_Kind kind;
    union {
        struct Event_Symb {
            Event_Kind kind;
            char title[MAX_TOKEN_LEN];
            char points_to[MAX_TOKEN_LEN];
        } event;

        struct Subprocess_Symb {
            char name[MAX_TOKEN_LEN];
        } subprocess;
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

bool contains(char *haystack, char needle, size_t limit) {
    for (size_t i = 0; i < limit && haystack[i] != '\0'; i++) {
        if (haystack[i] == needle) {
            return true;
        }
    }

    return false;
}

void symb_name(char *dest, char *namespace, char *name) {
    size_t i = 0;

    if (!contains(name, '.', MAX_TOKEN_LEN))  {
        for (; i < MAX_TOKEN_LEN && namespace[i] != '\0'; i++)
            dest[i] = namespace[i];

        dest[i++] = '.';
    }

    for (size_t j = 0; i < MAX_TOKEN_LEN && name[j] != '\0'; i++, j++) {
        dest[i] = name[j];
    }

    dest[i] = '\0';
}

size_t hash(char *key, size_t *key_len) {
    size_t h = 0;
    size_t i = 0;
    for (; key[i] != '\0'; i++) {
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

    return entry->occupied ? entry : NULL;
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
    TOKEN_OPTAG,
    TOKEN_CLTAG,
    TOKEN_SUBPROCESS,
    TOKEN_EVENTS,
    TOKEN_ID,
    TOKEN_STR,
    TOKEN_ATR,
    TOKEN_EOF,
    TOKEN_TYPE,
    TOKEN_SLASH,
    __TOKENS_COUNT
};

char *TOKEN_DESC[] = {
    [TOKEN_PROCESS] = "PROCESS", [TOKEN_SUBPROCESS] = "SUBPROCESS",
    [TOKEN_EVENTS] = "EVENTS",   [TOKEN_TYPE] = "TYPE",
    [TOKEN_ID] = "ID",           [TOKEN_STR] = "STRING",
    [TOKEN_ATR] = "ASSIGNMENT",  [TOKEN_OPTAG] = "OPEN TAG",
    [TOKEN_CLTAG] = "CLOSE TAG", [TOKEN_SLASH] = "SLASH",
    [TOKEN_EOF] = "EOF"};

_Static_assert(
    ARRAY_SIZE(TOKEN_DESC) == __TOKENS_COUNT,
    "Make sure that you have implemented description for new tokens!");

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
    if (key_len == table->head->key_len &&
        memcmp(table->head->key, key, key_len) == 0) {
        return table->head;
    }

    table->current = table->head;
    while (table->current->next != NULL) {
        if (key_len == table->current->next->key_len &&
            memcmp(table->current->next->key, key, key_len) == 0) {
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

#define NEW_KEYWORD(_key, token_kind)                                \
    {                                                                \
        .key = (_key), .key_len = ARRAY_SIZE((_key)) - 1, .token = { \
            .kind = (token_kind),                                    \
            .value = (_key)                                          \
        }                                                            \
    }

Keyword_Node keywords[] = {
    NEW_KEYWORD("process", TOKEN_PROCESS), NEW_KEYWORD("events", TOKEN_EVENTS),
    NEW_KEYWORD("task", TOKEN_TYPE), NEW_KEYWORD("starter", TOKEN_TYPE),
    NEW_KEYWORD("subprocess", TOKEN_SUBPROCESS)};

void build_keyword_table() {
    for (size_t i = 0; i < ARRAY_SIZE(keywords); i++) {
        set_keyword(&keyword_table, &keywords[i]);
    }
}

/*******************************************************************\
| Section: Lexer                                                    |
| TODO: Write somehting about the implementation of the lexer       |
\*******************************************************************/

#define PRINT_ERROR(lexer, msg)                                         \
    fprintf(stderr, "%s:%ld:%ld: error: " msg "\n", (lexer)->file_path, \
            (lexer)->row, (lexer)->col);
#define PRINT_ERROR_FMT(lexer, format, ...)                                \
    fprintf(stderr, "%s:%ld:%ld: error: " format "\n", (lexer)->file_path, \
            (lexer)->row, (lexer)->col, __VA_ARGS__);

typedef struct {
    char *content;
    size_t col, row;
    const char *file_path;
    Hash_Map symbols;
    Token token;
} Lexer;

void init_lexer(Lexer *lexer, const char *file_path) {
    lexer->col = 1;
    lexer->row = 1;
    lexer->file_path = file_path;
    lexer->content = read_file(file_path);
    lexer->symbols.len = 0;
    build_keyword_table();
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

char lex_peekc(Lexer *lexer) { return *lexer->content; }

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
        case '<':
            lexer->token.kind = TOKEN_OPTAG;
            break;
        case '>':
            lexer->token.kind = TOKEN_CLTAG;
            break;
        case '=':
            lexer->token.kind = TOKEN_ATR;
            break;
        case '/':
            lexer->token.kind = TOKEN_SLASH;
            break;

        case '\'': {
            len = 0;
            c = lex_getc(lexer);
            while (len < (MAX_TOKEN_LEN - 1) && c != '\0' && c != '\'' &&
                   c != '\n') {
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
            while (len < (MAX_TOKEN_LEN - 1) && c != '\0' &&
                   (isalnum(peek) || peek == '_')) {
                c = lex_getc(lexer);
                lexer->token.value[len++] = c;
                peek = lex_peekc(lexer);
            }

            lexer->token.value[len] = '\0';
            Keyword_Node *keyword =
                get_keyword(&keyword_table, lexer->token.value, len);
            if (keyword != NULL) {
                lexer->token.kind = keyword->token.kind;
            } else {
                lexer->token.kind = TOKEN_ID;
            }
        }
    }

    lexer->token.value[len] = '\0';
    return lexer->token;
}

void next_token_fail_if_eof(Lexer *lexer) {
    next_token(lexer);
    if (lexer->token.kind == TOKEN_EOF) {
        PRINT_ERROR(lexer, "Unexpected end of file");
        exit(EXIT_FAILURE);
    }
}

void assert_next_token(Lexer *lexer, enum Token_Kind expected) {
    next_token(lexer);
    if (lexer->token.kind != expected) {
        PRINT_ERROR_FMT(lexer, "Expected %s, found `%s`", TOKEN_DESC[expected],
                        lexer->token.value);
        exit(EXIT_FAILURE);
    }
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
} Screen_Object;

#define MAX_SCREEN_OBJECTS 512
#define HEADER_HEIGHT 30
#define SUB_HEADER_WIDTH 30
#define ROWS_PER_SUB 3
#define SUB_HEIGHT 300
#define SUB_WIDTH 1500

typedef struct {
    Screen_Object screen_objects[MAX_SCREEN_OBJECTS];
    Font font;
    int font_size;
    size_t objs_cnt;
    char title[MAX_TOKEN_LEN];
    int height, width;
    int cols, rows;
} Screen;

void init_screen(Screen *screen) {
    screen->cols = 10;
    screen->height = (screen->rows / ROWS_PER_SUB) * SUB_HEIGHT;
    screen->width = SUB_WIDTH;
}

size_t push_obj(Screen *screen, Screen_Object obj) {
    ASSERT(screen->objs_cnt < MAX_SCREEN_OBJECTS &&
           "TODO: maybe use a dynamic array?");
    screen->screen_objects[screen->objs_cnt] = obj;
    return screen->objs_cnt++;
}

#define RECT_POS(rect) (Vector2) { .x = (rect).x, .y = (rect).y }
#define OBJ_EVENT_PADDING 10
#define LINE_THICKNESS 1.2

Vector2 grid2world(Screen screen, Vector2 grid_pos, int obj_height, bool center, int padding) {
    Vector2 units = { screen.width / screen.cols, screen.height / screen.rows };

    Vector2 pos = (Vector2) {
        .x = grid_pos.x * units.x + padding + SUB_HEADER_WIDTH,
        .y = grid_pos.y*units.y + HEADER_HEIGHT
    };

    if (center) {
        pos.y += units.y*0.5 - obj_height*0.5;
    }

    return pos;
}

void draw_arrow(Screen screen, Screen_Object from, Screen_Object to) {
    Vector2 world_from =
        grid2world(screen, RECT_POS(from.rect), from.rect.height, true, OBJ_EVENT_PADDING);

    Vector2 v2_from = {.x = world_from.x + from.rect.width,
                       .y = world_from.y + from.rect.height / 2};

    Vector2 world_to = grid2world(screen, RECT_POS(to.rect), to.rect.height, true, OBJ_EVENT_PADDING);
    Vector2 v2_to = {.x = world_to.x, .y = world_to.y + to.rect.height / 2};

    DrawLineEx(v2_from, v2_to, LINE_THICKNESS, BLACK);
    Vector2 v2_point = v2_to;
    v2_point.x -= 4;
    DrawCircleV(v2_point, 4, BLACK);
}

int measure_text_lines(Rectangle rect, const char **words, int word_count,
                       int font_size) {
    int space_left = rect.width;
    Vector2 pos = {.x = rect.x, .y = rect.y};
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

void draw_fitting_text(Rectangle rect, Font font, char *text, int font_size,
                       int margin) {
    int word_count = 0;
    const char **words = TextSplit(text, ' ', &word_count);
    int space_left = rect.width;
    int total_lines = measure_text_lines(rect, words, word_count, font_size);
    if ((total_lines * font_size) > rect.height) {
        font_size = rect.height / total_lines;
    }

    rect.x += margin;
    rect.y += margin;
    rect.width -= margin * 2;
    rect.height -= margin * 2;
    Vector2 pos = {.x = rect.x, .y = rect.y};
    const float spacing = font_size / 10.0;
    for (int i = 0; i < word_count; i++) {
        int word_len =
            MeasureTextEx(font, words[i], font_size, spacing).x + font_size;

        if (word_len > space_left) {
            space_left = rect.width - word_len;
            pos.y += font_size;
            pos.x = rect.x;
        } else {
            space_left -= word_len;
        }

        DrawTextEx(font, words[i], pos, font_size, spacing, BLACK);
        pos.x += word_len;
    }
}

#define VECTOR(vx, vy) (Vector2) {.x=(vx), .y=(vy)}
void draw_header(Screen screen) {
    DrawLineEx(VECTOR(0, HEADER_HEIGHT), VECTOR(screen.width, HEADER_HEIGHT), LINE_THICKNESS, BLACK);
    const float font_size = screen.font_size * 1.5;
    const float spacing = font_size / 10.0;
    Vector2 textMeasure =
        MeasureTextEx(screen.font, screen.title, font_size, spacing);

    Vector2 pos = {.x = screen.width / 2 - textMeasure.x / 2,
                   .y = HEADER_HEIGHT / 2 - textMeasure.y / 2};

    DrawTextEx(screen.font, screen.title, pos, font_size, font_size / 10,
               BLACK);
}

void draw_obj(Screen screen, Screen_Object obj) {
    if (obj.value->kind == SYMB_EVENT) {
        Vector2 world_obj_pos =
            grid2world(screen, RECT_POS(obj.rect), obj.rect.height, true, OBJ_EVENT_PADDING);

        Rectangle world_obj_rect = {.x = world_obj_pos.x,
                                    .y = world_obj_pos.y,
                                    .width = obj.rect.width,
                                    .height = obj.rect.height};

        switch (obj.value->as.event.kind) {
            case EVENT_STARTER: {
                Vector2 pos = {world_obj_rect.x, world_obj_rect.y};
                pos.x += world_obj_rect.width / 2;
                pos.y += world_obj_rect.height / 2;
                DrawCircleV(pos, world_obj_rect.width / 2, GREEN);
                DrawRectangleRoundedLinesEx(world_obj_rect, 0.3f, 0, 1, BLACK);
            } break;

            case EVENT_TASK: {
                DrawRectangleRoundedLinesEx(world_obj_rect, 0.3f, 0, LINE_THICKNESS, BLACK);
                draw_fitting_text(world_obj_rect, screen.font,
                                  obj.value->as.event.title, screen.font_size,
                                  5);
            } break;

            default:
                ASSERT(0 && "Unreachable statement");
        }

        if (obj.points_to >= 0) {
            draw_arrow(screen, obj, screen.screen_objects[obj.points_to]);
        }

    } else if (obj.value->kind == SYMB_SUBPROCESS) {
        Vector2 world_obj_pos =
            grid2world(screen, RECT_POS(obj.rect), obj.rect.height, false, 0);

        Rectangle entire_row = {
            .x = world_obj_pos.x - SUB_HEADER_WIDTH,
            .y = world_obj_pos.y,
            .width = obj.rect.width - 1,
            .height = obj.rect.height
        };

        Rectangle sub_header = {
            .x = world_obj_pos.x - SUB_HEADER_WIDTH,
            .y = world_obj_pos.y,
            .width = SUB_HEADER_WIDTH,
            .height = obj.rect.height
        };


        DrawRectangleLinesEx(entire_row, LINE_THICKNESS, BLACK);
        DrawRectangleLinesEx(sub_header, LINE_THICKNESS, BLACK);
    }
}

/*******************************************************************\
| Section: Parser                                                   |
| TODO: Write somehting about the implementation of the Parser      |
\*******************************************************************/
void parse(Lexer *lexer, Screen *screen);
void parse_process(Lexer *lexer, Screen *screen);
void parse_subprocess(Lexer *lexer, Screen *screen);
void parse_events(Lexer *lexer, Screen *screen, char *namespace);
void track_arrows(Lexer *lexer, Screen *screen);

void parse(Lexer *lexer, Screen *screen) {
    parse_process(lexer, screen);

    for (;;) {
        assert_next_token(lexer, TOKEN_OPTAG);
        next_token_fail_if_eof(lexer);
        if (lexer->token.kind == TOKEN_SLASH) {
            assert_next_token(lexer, TOKEN_PROCESS);
            assert_next_token(lexer, TOKEN_CLTAG);
            break;
        }

        parse_subprocess(lexer, screen);
    }

    track_arrows(lexer, screen);
}

void parse_process(Lexer *lexer, Screen *screen) {
    next_token(lexer);
    if (lexer->token.kind != TOKEN_OPTAG) {
        PRINT_ERROR_FMT(lexer, "Expected new tag, find `%s`",
                        lexer->token.value);
        exit(EXIT_FAILURE);
    }

    next_token_fail_if_eof(lexer);
    if (lexer->token.kind != TOKEN_PROCESS) {
        PRINT_ERROR_FMT(lexer, "Expected tag process, find `%s`",
                        lexer->token.value);
        exit(EXIT_FAILURE);
    }

    next_token_fail_if_eof(lexer);
    if (lexer->token.kind != TOKEN_ID) {
        PRINT_ERROR(lexer, "Process need to have an `name` attribute");
        exit(EXIT_FAILURE);
    }

    const char *token_value = lexer->token.value;
    if (strncmp(token_value, "name", 5) != 0) {
        PRINT_ERROR_FMT(lexer, "Invalid attribute `%s` for tag process",
                        lexer->token.value);
        exit(EXIT_SUCCESS);
    }

    assert_next_token(lexer, TOKEN_ATR);
    assert_next_token(lexer, TOKEN_STR);
    memcpy(screen->title, lexer->token.value, MAX_TOKEN_LEN);

    assert_next_token(lexer, TOKEN_CLTAG);
}

void parse_subprocess(Lexer *lexer, Screen *screen) {
    if (lexer->token.kind != TOKEN_SUBPROCESS) {
        PRINT_ERROR(lexer, "Expected new subprocess or end of process");
        exit(EXIT_FAILURE);
    }

    bool id_finded = false;
    char subprocess_namespace[MAX_TOKEN_LEN] = {0};
    char subprocess_name[MAX_TOKEN_LEN] = {0};
    for (;;) {
        next_token_fail_if_eof(lexer);
        if (lexer->token.kind == TOKEN_CLTAG) break;
        if (lexer->token.kind != TOKEN_ID) {
            PRINT_ERROR(lexer, "Syntax error");
            exit(EXIT_FAILURE);
        }

        if (strncmp(lexer->token.value, "id", 3) == 0) {
            assert_next_token(lexer, TOKEN_ATR);
            assert_next_token(lexer, TOKEN_STR);
            memcpy(subprocess_namespace, lexer->token.value, MAX_TOKEN_LEN);
            id_finded = true;
        } else if (strncmp(lexer->token.value, "name", 5) == 0) {
            assert_next_token(lexer, TOKEN_ATR);
            assert_next_token(lexer, TOKEN_STR);
            memcpy(subprocess_name, lexer->token.value, MAX_TOKEN_LEN);
        } else {
            PRINT_ERROR_FMT(lexer,
                            "Unexpected attribute name %s for subprocess",
                            lexer->token.value);
            exit(EXIT_FAILURE);
        }
    }

    if (!id_finded) {
        PRINT_ERROR(lexer, "Subprocess must have an `id`");
        exit(EXIT_FAILURE);
    }

    Symbol symbol = {.kind = SYMB_SUBPROCESS, .obj_id = -1};
    hm_put(&lexer->symbols, subprocess_namespace, symbol);
    Key_Value *entry = hm_get(&lexer->symbols, subprocess_namespace);
    assert(entry != NULL);

    parse_events(lexer, screen, subprocess_namespace);

    Screen_Object subprocess_obj = {
        .value = &entry->value,
        .rect = {
            .width = SUB_WIDTH,
            .height = SUB_HEIGHT,
            .x = 0,
            .y = screen->rows
        },
        .points_to = -1
    };

    screen->rows += ROWS_PER_SUB;

    entry->value.obj_id = push_obj(screen, subprocess_obj);
    memcpy(entry->value.as.subprocess.name, subprocess_name, MAX_TOKEN_LEN);

    assert_next_token(lexer, TOKEN_OPTAG);
    assert_next_token(lexer, TOKEN_SLASH);
    assert_next_token(lexer, TOKEN_SUBPROCESS);
    assert_next_token(lexer, TOKEN_CLTAG);
}

Event_Kind translate_event(const char *event) {
    if (strcmp(event, "starter") == 0) return EVENT_STARTER;
    if (strcmp(event, "task") == 0) return EVENT_TASK;

    return EVENT_INVALID;
}

void parse_events(Lexer *lexer, Screen *screen, char *namespace) {
    assert_next_token(lexer, TOKEN_OPTAG);
    assert_next_token(lexer, TOKEN_EVENTS);
    assert_next_token(lexer, TOKEN_CLTAG);

    int col = 0;
    for (;;) {
        assert_next_token(lexer, TOKEN_OPTAG);
        next_token_fail_if_eof(lexer);
        if (lexer->token.kind == TOKEN_TYPE) {
            Event_Kind event_kind = translate_event(lexer->token.value);
            if (event_kind == EVENT_INVALID) {
                PRINT_ERROR(lexer, "Invalid event type");
                exit(EXIT_FAILURE);
            }

            Symbol symbol = {
                .obj_id = -1,
                .as.event.kind = event_kind,
                .kind = SYMB_EVENT
            };

            char buffer[MAX_TOKEN_LEN];
            Key_Value *kv = NULL;
            bool id_founded = false;
            for (;;) {
                next_token_fail_if_eof(lexer);
                if (lexer->token.kind == TOKEN_SLASH) {
                    assert_next_token(lexer, TOKEN_CLTAG);
                    break;
                }

                if (lexer->token.kind != TOKEN_ID) {
                    PRINT_ERROR(lexer, "Syntax error");
                    exit(EXIT_FAILURE);
                }

                memcpy(buffer, lexer->token.value, MAX_TOKEN_LEN);
                assert_next_token(lexer, TOKEN_ATR);
                assert_next_token(lexer, TOKEN_STR);

                if (!id_founded && strncmp(buffer, "id", 3) == 0) {
                    symb_name(buffer, namespace, lexer->token.value);
                    hm_put(&lexer->symbols, buffer, symbol);
                    kv = hm_get(&lexer->symbols, buffer);
                    id_founded = true;
                } else if (id_founded && kv != NULL) {
                    if (strncmp(buffer, "name", 5) == 0) {
                        memcpy(kv->value.as.event.title, lexer->token.value,MAX_TOKEN_LEN);
                    } else if (strncmp(buffer, "points", 7) == 0) {
                        symb_name(buffer, namespace, lexer->token.value);
                        memcpy(kv->value.as.event.points_to, buffer, MAX_TOKEN_LEN);
                    } else {
                        PRINT_ERROR_FMT(lexer, "Invalid event attribute `%s`",buffer);
                        exit(EXIT_FAILURE);
                    }
                } else {
                    PRINT_ERROR_FMT(
                        lexer,
                        "Invalid event attribute `%s`. Event `id` has to be "
                        "the first attribute",
                        buffer);
                    exit(EXIT_FAILURE);
                }
            }

            if (!id_founded) {
                PRINT_ERROR(lexer, "Event need to have and `id`");
                exit(EXIT_FAILURE);
            }

            Screen_Object obj = {
                .rect = {.height = 60, .width = 80, .x = col++, .y = screen->rows + 1},
                .value = &kv->value,
                .points_to = -1
            };

            if (event_kind == EVENT_STARTER) {
                obj.rect.height = 40;
                obj.rect.width = 40;
            }

            kv->value.obj_id = push_obj(screen, obj);
        } else if (lexer->token.kind == TOKEN_SLASH) {
            next_token_fail_if_eof(lexer);
            if (lexer->token.kind == TOKEN_EVENTS) {
                assert_next_token(lexer, TOKEN_CLTAG);
                break;
            }

            PRINT_ERROR_FMT(lexer,
                            "Unexpected closing tag %s. Perhaps you want to "
                            "close `events`?",
                            lexer->token.value);
            exit(EXIT_FAILURE);
        } else {
            PRINT_ERROR_FMT(lexer, "Unexpected tag `<%s`", lexer->token.value);
            exit(EXIT_FAILURE);
        }
    }
}

void track_arrows(Lexer *lexer, Screen *screen) {
    for (size_t i = 0; i < HASHMAP_CAPACITY; i++) {
        if (lexer->symbols.entries[i].occupied &&
            lexer->symbols.entries[i].value.kind == SYMB_EVENT &&
            lexer->symbols.entries[i].value.obj_id >= 0 &&
            lexer->symbols.entries[i].value.as.event.points_to[0] != '\0')
        {
            Screen_Object *from =
                &screen->screen_objects[lexer->symbols.entries[i].value.obj_id];

            Key_Value *to =
                hm_get(&lexer->symbols,
                       lexer->symbols.entries[i].value.as.event.points_to);

            if (to != NULL && to->occupied && to->value.obj_id >= 0) {
                from->points_to = to->value.obj_id;
            }
        }
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

    // next_token(&lexer);
    // while (lexer.token.kind != TOKEN_EOF) {
    //     printf("(`%s`, %s)\n", lexer.token.value,
    //     TOKEN_DESC[lexer.token.kind]); next_token(&lexer);
    // }

    parse(&lexer, &screen);
    init_screen(&screen);

    InitWindow(screen.width, screen.height + HEADER_HEIGHT, screen.title);
    screen.font = LoadFont("./resources/Cascadia.ttf");
    screen.font_size = 11;
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(WHITE);
        draw_header(screen);
        for (size_t i = 0; i < screen.objs_cnt; i++) {
            draw_obj(screen, screen.screen_objects[i]);
        }

        // DrawLine(0, (screen.height/2) + HEADER_HEIGHT, screen.width, (screen.height/2) + HEADER_HEIGHT, RED);

        EndDrawing();
    }

    CloseWindow();

    return EXIT_SUCCESS;
}
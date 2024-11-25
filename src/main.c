#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bundle.c"
#include "raylib.h"
#include "raymath.h"

#define ASSERT assert
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define FAIL exit(EXIT_FAILURE)

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

    fclose(file);
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

typedef enum {
    EVENT_STARTER = 0,
    EVENT_TASK,
    EVENT_GATEWAY,
    EVENT_INVALID,
    EVENT_WAIT,
    EVENT_MAIL,
    EVENT_END
} Event_Kind;

typedef enum {
    SYMB_EVENT = 0,
    SYMB_SUBPROCESS
} Symb_Kind;

typedef struct {
    union {

        struct Event_Symb {
            Event_Kind kind;
            char title[MAX_TOKEN_LEN];
            char points_to[3][MAX_TOKEN_LEN];
        } event;

        struct Subprocess_Symb {
            char name[MAX_TOKEN_LEN];
        } subprocess;

    } as;

    Symb_Kind kind;
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

bool str_contains(char *haystack, char needle, size_t limit) {
    for (size_t i = 0; i < limit && haystack[i] != '\0'; i++) {
        if (haystack[i] == needle) {
            return true;
        }
    }

    return false;
}

// appends namespace to symbol if not already contains it
void symb_name(char *dest, char *namespace, char *name) {
    size_t i = 0;

    if (!str_contains(name, '.', MAX_TOKEN_LEN))  {
        for (; i < MAX_TOKEN_LEN && namespace[i] != '\0'; i++) {
            dest[i] = namespace[i];
        }

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

Key_Value *get_symbol(Hash_Map *map, char *key) {
    size_t key_len = 0;
    size_t h = hash(key, &key_len);

    Key_Value *entry = &map->entries[HASHMAP_INDEX(h)];
    while (entry->occupied && memcmp(entry->key, key, key_len) != 0) {
        h++;
        entry = &map->entries[HASHMAP_INDEX(h)];
    }

    return entry->occupied ? entry : NULL;
}

Key_Value* put_symbol(Hash_Map *map, char *key, Symbol symbol) {
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
        entry->value.obj_id = -1;
    }

    entry->value = symbol;
    return entry;
}

/*******************************************************************\
| Section: Tokens                                                   |
\*******************************************************************/

typedef enum {
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
    TOKEN_COL,
    TOKEN_SLASH,
    __TOKENS_COUNT
} Token_Kind;

char *TOKEN_DESC[]  = {
    [TOKEN_PROCESS]     = "PROCESS",
    [TOKEN_SUBPROCESS]  = "SUBPROCESS",
    [TOKEN_EVENTS]      = "EVENTS",
    [TOKEN_TYPE]        = "TYPE",
    [TOKEN_ID]          = "ID",
    [TOKEN_STR]         = "STRING",
    [TOKEN_COL]         = "COLUMN",
    [TOKEN_ATR]         = "ASSIGNMENT",
    [TOKEN_OPTAG]       = "OPEN TAG",
    [TOKEN_CLTAG]       = "CLOSE TAG",
    [TOKEN_SLASH]       = "SLASH",
    [TOKEN_EOF]         = "EOF"
};

_Static_assert(
    ARRAY_SIZE(TOKEN_DESC) == __TOKENS_COUNT,
    "Make sure that you have implemented description for new tokens!"
);

typedef struct {
    char value[MAX_TOKEN_LEN];
    Token_Kind kind;
} Token;

/*******************************************************************\
| Section: Key Words Table                                          |
\*******************************************************************/

typedef struct {
    char *key;
    size_t key_len;
    Token_Kind kind;
} Keyword;

#define NEW_KEYWORD(_key, token_kind)                     \
    {                                                     \
        .key = (_key), .key_len = ARRAY_SIZE((_key)) - 1, \
        .kind = (token_kind),                             \
    }

static Keyword keywords[] = {
    NEW_KEYWORD("process", TOKEN_PROCESS),
    NEW_KEYWORD("events", TOKEN_EVENTS),
    NEW_KEYWORD("col", TOKEN_COL),
    NEW_KEYWORD("task", TOKEN_TYPE),
    NEW_KEYWORD("gateway", TOKEN_TYPE),
    NEW_KEYWORD("wait", TOKEN_TYPE),
    NEW_KEYWORD("mail", TOKEN_TYPE),
    NEW_KEYWORD("end", TOKEN_TYPE),
    NEW_KEYWORD("starter", TOKEN_TYPE),
    NEW_KEYWORD("subprocess", TOKEN_SUBPROCESS)
};

Token_Kind get_kind(char *key, size_t key_len) {
    for (size_t i = 0; i < ARRAY_SIZE(keywords); i++) {
        Keyword kw = keywords[i];
        if (key_len == kw.key_len && strncmp(key, kw.key, key_len) == 0) {
            return kw.kind;
        }
    }

    return TOKEN_ID;
}

/*******************************************************************\
| Section: Lexer                                                    |
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
}

char lex_getc(Lexer *lexer) {
    char c = *lexer->content;
    if (c == '\0') {
        return '\0';
    }

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
        case '<': lexer->token.kind = TOKEN_OPTAG; break;
        case '>': lexer->token.kind = TOKEN_CLTAG; break;
        case '=': lexer->token.kind = TOKEN_ATR;   break;
        case '/': lexer->token.kind = TOKEN_SLASH; break;

        case '\'': {
            len = 0;
            c = lex_getc(lexer);
            while (len < (MAX_TOKEN_LEN - 1) && c != '\0' && c != '\'' && c != '\n') {
                lexer->token.value[len++] = c;
                c = lex_getc(lexer);
            }

            if (c != '\'') {
                PRINT_ERROR(lexer, "Unexpected end of string literal");
                FAIL;
            }

            lexer->token.kind = TOKEN_STR;
        } break;

        default: {
            if (!isalpha(c)) {
                PRINT_ERROR_FMT(lexer, "Invalid character `%c`", c);
                FAIL;
            }

            char peek = lex_peekc(lexer);
            while (len < (MAX_TOKEN_LEN - 1) && c != '\0' && (isalnum(peek) || peek == '_')) {
                c = lex_getc(lexer);
                lexer->token.value[len++] = c;
                peek = lex_peekc(lexer);
            }

            lexer->token.value[len] = '\0';
            lexer->token.kind = get_kind(lexer->token.value, len);
        }
    }

    lexer->token.value[len] = '\0';
    return lexer->token;
}

void next_token_fail_if_eof(Lexer *lexer) {
    next_token(lexer);
    if (lexer->token.kind == TOKEN_EOF) {
        PRINT_ERROR(lexer, "Unexpected end of file");
        FAIL;
    }
}

void assert_next_token(Lexer *lexer, Token_Kind expected) {
    next_token(lexer);
    if (lexer->token.kind != expected) {
        PRINT_ERROR_FMT(lexer, "Expected %s, found `%s`", TOKEN_DESC[expected], lexer->token.value);
        FAIL;
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

#define RECT_POS(rect) (Vector2) { .x = (rect).x, .y = (rect).y }
#define VECTOR(vx, vy) (Vector2) { .x = (vx), .y = (vy) }

typedef struct {
    Rectangle rect;
    Symbol *value;
} Screen_Object;

#define MAX_SCREEN_OBJECTS 512
typedef struct {
    Screen_Object screen_objects[MAX_SCREEN_OBJECTS];
    size_t objs_cnt;
    char title[MAX_TOKEN_LEN];
    int cols, rows;

    Texture2D wait_texture;
    Texture2D mail_texture;
    Texture2D rectangle;
    Texture2D rect_rexture;

    Font font;
    Font font_header;

    struct {
        int font_size;
        int font_size_header;
        float line_thickness;
        int height, width;
        int header_height;
        int sub_header_width;
        int rows_per_sub;
        int sub_height;
        int sub_width;
        int events_padding;
    } settings;
} Screen;

void init_screen(Screen *screen) {
    screen->cols = 10;
    screen->settings.header_height = 30;
    screen->settings.sub_header_width = 30;
    screen->settings.rows_per_sub = 3;
    screen->settings.sub_height = 300;
    screen->settings.sub_width = 150 * screen->cols;
    screen->settings.line_thickness = 2;
    screen->settings.events_padding = 10;
}

void setup_screen(Screen *screen) {
    screen->settings.height = (screen->rows / screen->settings.rows_per_sub) * screen->settings.sub_height;
    screen->settings.width = screen->settings.sub_width;
    screen->settings.font_size = 13;
    screen->settings.font_size_header = screen->settings.font_size*1.5;
}

size_t push_obj(Screen *screen, Screen_Object obj) {
    ASSERT(screen->objs_cnt < MAX_SCREEN_OBJECTS && "out of space");
    screen->screen_objects[screen->objs_cnt] = obj;
    return screen->objs_cnt++;
}


Vector2 grid2world(Screen screen, Vector2 grid_pos, int obj_height, bool center, int padding) {
    Vector2 units = {
        screen.settings.width / screen.cols,
        screen.settings.height / screen.rows
    };

    Vector2 pos = (Vector2) {
        .x = grid_pos.x*units.x + padding + screen.settings.sub_header_width,
        .y = grid_pos.y*units.y + screen.settings.header_height
    };

    if (center) {
        pos.y += units.y*0.5 - obj_height*0.5;
        pos.x += units.x*0.5 - obj_height*0.5;
    }

    return pos;
}

void draw_arrow_head(Screen screen, Vector2 start, Vector2 end) {
    const int head_size = 6;
    Vector2 direction = Vector2Subtract(end, start);
    float total_length = Vector2Length(direction);

    if (total_length > 0) {
        direction = Vector2Scale(direction, 1.0f / total_length);

        Vector2 adjusted_end = Vector2Add(start, Vector2Scale(direction, total_length - head_size * 2));

        Vector2 perpendicular = (Vector2){ -direction.y, direction.x };
        Vector2 right_point = Vector2Add(adjusted_end, Vector2Scale(perpendicular, -head_size));
        Vector2 left_point = Vector2Add(adjusted_end, Vector2Scale(perpendicular, head_size));
        Vector2 arrow_head_base = Vector2Add(adjusted_end, Vector2Scale(direction, head_size * 2));

        DrawLineEx(start, adjusted_end, screen.settings.line_thickness, BLACK);
        DrawLineEx(adjusted_end, left_point, screen.settings.line_thickness, BLACK);
        DrawLineEx(adjusted_end, right_point, screen.settings.line_thickness, BLACK);
        DrawLineEx(left_point, arrow_head_base, screen.settings.line_thickness, BLACK);
        DrawLineEx(right_point, arrow_head_base, screen.settings.line_thickness, BLACK);
    }
}

void draw_arrow(Screen screen, Screen_Object from, Screen_Object to) {
    Vector2 world_from = grid2world(screen, RECT_POS(from.rect), from.rect.height, true, screen.settings.events_padding);
    Vector2 world_to = grid2world(screen, RECT_POS(to.rect), to.rect.height, true, screen.settings.events_padding);

    Vector2 start = {0};
    Vector2 three_lines = {0};
    Vector2 end = {0};

    int diff_iy = from.rect.y - to.rect.y;
    int diff_ix = from.rect.x - to.rect.x;
    if (diff_iy == 0) { // mesma linha
        if (diff_ix < 0) { // from atras
            start.y = world_from.y + from.rect.height/2.0;
            start.x = world_from.x + from.rect.width;
            end.y = start.y;
            end.x = world_to.x;
        } else { // from na frente
            start.y = world_from.y + from.rect.height/2.0;
            start.x = world_from.x;
            end.y = start.y;
            end.x = world_to.x + to.rect.width;
        }
    } else if (diff_iy < 0) { // from acima
        if (diff_ix < 0) { // from atras
            three_lines = (Vector2) {
                .y = world_from.y + from.rect.height,
                .x = world_from.x + from.rect.width/2.0
            };
            end.x = world_to.x;
            end.y = world_to.y + to.rect.height/2.0;
            start.x = three_lines.x;
            start.y = end.y;
            DrawLineEx(three_lines, start, screen.settings.line_thickness, BLACK);
        } else if (diff_ix > 0) { // from na frente
            three_lines = (Vector2) {
                .y = world_from.y + from.rect.height,
                .x = world_from.x + from.rect.width/2.0
            };
            end.x = world_to.x + to.rect.width;
            end.y = world_to.y + to.rect.height/2.0;
            start.x = three_lines.x;
            start.y = end.y;
            DrawLineEx(three_lines, start, screen.settings.line_thickness, BLACK);
        } else {
            start.x = end.x = world_from.x + from.rect.width/2.0;
            start.y = world_from.y + from.rect.height;
            end.y = world_to.y;
        }
    } else { // from abaixo
        if (diff_ix < 0) { // from atras
            three_lines = (Vector2) {
                .y = world_from.y,
                .x = world_from.x + from.rect.width/2.0
            };
            end.x = world_to.x;
            end.y = world_to.y + to.rect.height/2.0;
            start.x = three_lines.x;
            start.y = end.y;
            DrawLineEx(three_lines, start, screen.settings.line_thickness, BLACK);
        } else if (diff_ix > 0) { // from na frente
            three_lines = (Vector2) {
                .y = world_from.y,
                .x = world_from.x + from.rect.width/2.0
            };
            end.x = world_to.x + to.rect.width;
            end.y = world_to.y + to.rect.height/2.0;
            start.x = three_lines.x;
            start.y = end.y;
            DrawLineEx(three_lines, start, screen.settings.line_thickness, BLACK);
        } else {
            start.x = end.x = world_from.x + from.rect.width/2.0;
            start.y = world_from.y;
            end.y = world_to.y + to.rect.height;
        }
    }

    draw_arrow_head(screen, start, end);
}

int count_text_lines(Rectangle rect, const char **words, int word_count, int font_size) {
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

void draw_fitting_text(Rectangle rect, Font font, char *text, int font_size, int margin) {
    int word_count = 0;
    const char **words = TextSplit(text, ' ', &word_count);

    int total_lines = count_text_lines(rect, words, word_count, font_size);
    if ((total_lines * font_size) > rect.height) {
        font_size = rect.height / total_lines;
    }

    rect.x += margin;
    rect.y += margin;
    rect.width -= margin * 2;
    rect.height -= margin * 2;
    Vector2 pos = {.x = rect.x, .y = rect.y};

    const float spacing = font_size / 8.0;
    int space_left = rect.width;
    for (int i = 0; i < word_count; i++) {
        int word_len = MeasureTextEx(font, words[i], font_size, spacing).x + font_size;

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

void draw_header(Screen screen) {
    const float spacing = screen.settings.font_size_header / 10.0;
    Vector2 text_measure = MeasureTextEx(screen.font_header, screen.title, screen.settings.font_size_header, spacing);

    Vector2 pos = {
        .x = screen.settings.width / 2 - text_measure.x / 2,
        .y = screen.settings.header_height / 2 - text_measure.y / 2
    };

    // DrawLineEx(VECTOR(0, screen.settings.header_height), VECTOR(screen.settings.width, screen.settings.header_height), screen.settings.line_thickness, BLACK);
    DrawTextEx(screen.font_header, screen.title, pos, screen.settings.font_size_header, spacing, BLACK);
}

void draw_subprocess_header(Screen screen, Screen_Object subprocess_obj) {
    Vector2 world_obj_pos = grid2world(screen, RECT_POS(subprocess_obj.rect), subprocess_obj.rect.height, false, 0);

    Rectangle entire_row = {
        .x = world_obj_pos.x - screen.settings.sub_header_width,
        .y = world_obj_pos.y,
        .width = subprocess_obj.rect.width - 1,
        .height = subprocess_obj.rect.height + 1
    };

    Rectangle sub_header = {
        .x = world_obj_pos.x - screen.settings.sub_header_width,
        .y = world_obj_pos.y,
        .width = screen.settings.sub_header_width,
        .height = subprocess_obj.rect.height + 1
    };

    const float spacing = screen.settings.font_size_header / 10.0;
    const float rotation = -90;

    Vector2 text_measure = MeasureTextEx(screen.font_header, subprocess_obj.value->as.subprocess.name, screen.settings.font_size_header, spacing);
    Vector2 text_position = RECT_POS(sub_header);
    text_position.y += sub_header.height/2.0 + text_measure.x/2.0;
    text_position.x += sub_header.width/2.0 - text_measure.y/2.0;

    DrawRectangleLinesEx(entire_row, screen.settings.line_thickness/2.0, BLACK);
    DrawRectangleLinesEx(sub_header, screen.settings.line_thickness/2.0, BLACK);
    DrawTextPro(screen.font_header, subprocess_obj.value->as.subprocess.name, text_position, (Vector2) {0}, rotation, screen.settings.font_size_header, spacing, BLACK);
}

void draw_obj(Screen screen, Screen_Object obj) {
    if (obj.value->kind == SYMB_EVENT) {
        Vector2 world_obj_pos = grid2world(screen, RECT_POS(obj.rect), obj.rect.height, true, screen.settings.events_padding);

        Rectangle world_obj_rect = {
            .x = world_obj_pos.x,
            .y = world_obj_pos.y,
            .width = obj.rect.width,
            .height = obj.rect.height
        };

        switch (obj.value->as.event.kind) {
            case EVENT_STARTER: {
                Vector2 pos = {world_obj_rect.x, world_obj_rect.y};
                pos.x += world_obj_rect.width / 2;
                pos.y += world_obj_rect.height / 2;
                DrawCircleV(pos, world_obj_rect.width / 2, GREEN);
                // DrawRectangleRoundedLinesEx(world_obj_rect, 0.3f, 0, 1, BLACK); // debug
            } break;

            case EVENT_TASK: {
                DrawRectangleRounded(world_obj_rect, 0.3f, 0, WHITE);
                DrawRectangleRoundedLinesEx(world_obj_rect, 0.3f, 0, screen.settings.line_thickness, BLACK);
                draw_fitting_text(world_obj_rect, screen.font, obj.value->as.event.title, screen.settings.font_size, 5);
            } break;

            case EVENT_GATEWAY: {
                DrawTexture(screen.rect_rexture, world_obj_pos.x, world_obj_pos.y, WHITE);
            } break;

            case EVENT_END: {
                Vector2 pos = {world_obj_rect.x, world_obj_rect.y};
                pos.x += world_obj_rect.width / 2;
                pos.y += world_obj_rect.height / 2;
                DrawCircleV(pos, world_obj_rect.width / 2, RED);
            } break;

            case EVENT_WAIT: {
                DrawTextureEx(screen.wait_texture, world_obj_pos, 0, 1.0f, WHITE);
            } break;

            case EVENT_MAIL: {
                DrawTextureEx(screen.mail_texture, world_obj_pos, 0, 1.0f, WHITE);
            } break;

            default: ASSERT(0 && "Unreachable statement");
        }

    } else if (obj.value->kind == SYMB_SUBPROCESS) {
        draw_subprocess_header(screen, obj);
    }
}

/*******************************************************************\
| Section: Parser                                                   |
\*******************************************************************/

#define MAX_ATTRS 128

typedef struct {
    char id[MAX_TOKEN_LEN];
    char value[MAX_TOKEN_LEN];
} Attr;


typedef struct {
    Attr items[MAX_ATTRS];
    size_t len;
} Attr_List;

Attr *get_attr(Attr_List attrs, char *id) {
    size_t len_id = strlen(id);
    ASSERT(len_id < MAX_TOKEN_LEN && "Length of id cannot be greater than MAX_TOKEN_LEN");

    for (size_t i = 0; i < attrs.len; i++) {
        Attr *attr = &attrs.items[i];
        if (strncmp(attr->id, id, len_id) == 0) {
            return attr;
        }
    }

    return NULL;
}

void parse(Lexer *lexer, Screen *screen);
void parse_process(Lexer *lexer, Screen *screen);
void parse_subprocess(Lexer *lexer, Screen *screen);
void parse_events(Lexer *lexer, Screen *screen, char *namespace);
void parse_columns(Lexer *lexer, Screen *screen, int col, char *namespace);

void parse_event(Lexer *lexer, Screen *screen, int col, char *namespace);
void parse_attrs(Lexer *lexer, Attr_List *attrs);

Screen_Object parse_event_task(Lexer *lexer, Attr_List attrs, Key_Value *symbol,  Screen *screen, int col, char *namespace);
Screen_Object parse_event_starter(Lexer *lexer, Attr_List attrs, Key_Value *symbol, Screen *screen, int col, char *namespace);
Screen_Object parse_event_with_sprite(Lexer *lexer, Attr_List attrs, Key_Value *symbol, Screen *screen, int col, char *namespace);
Screen_Object parse_event_gateway(Attr_List attrs, Key_Value *symbol, Screen *screen, int col, char *namespace);
Screen_Object parse_event_end(Screen *screen, int col);

Event_Kind translate_event(const char *event);
int translate_row(Lexer *lexer, const char *column);

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
}

void parse_process(Lexer *lexer, Screen *screen) {
    next_token(lexer);
    if (lexer->token.kind != TOKEN_OPTAG) {
        PRINT_ERROR_FMT(lexer, "Expected new tag, find `%s`", lexer->token.value);
        FAIL;
    }

    next_token_fail_if_eof(lexer);
    if (lexer->token.kind != TOKEN_PROCESS) {
        PRINT_ERROR_FMT(lexer, "Expected tag process, find `%s`", lexer->token.value);
        FAIL;
    }

    next_token_fail_if_eof(lexer);
    if (lexer->token.kind != TOKEN_ID) {
        PRINT_ERROR(lexer, "Process need to have an `name` attribute");
        FAIL;
    }

    const char *token_value = lexer->token.value;
    if (strncmp(token_value, "name", 5) != 0) {
        PRINT_ERROR_FMT(lexer, "Invalid attribute `%s` for tag process", lexer->token.value);
        FAIL;
    }

    assert_next_token(lexer, TOKEN_ATR);
    assert_next_token(lexer, TOKEN_STR);
    memcpy(screen->title, lexer->token.value, MAX_TOKEN_LEN);

    assert_next_token(lexer, TOKEN_CLTAG);
}

void parse_subprocess(Lexer *lexer, Screen *screen) {
    if (lexer->token.kind != TOKEN_SUBPROCESS) {
        PRINT_ERROR(lexer, "Expected new subprocess or end of process");
        FAIL;
    }

    Symbol symbol = {
        .kind = SYMB_SUBPROCESS,
        .obj_id = -1
    };

    Attr_List attrs = {0};
    char subprocess_namespace[MAX_TOKEN_LEN] = {0};

    parse_attrs(lexer, &attrs);
    Attr *id_attr = get_attr(attrs, "id");
    if (id_attr == NULL) {
        PRINT_ERROR(lexer, "Subprocess must have  an `id`");
        FAIL;
    }

    memcpy(subprocess_namespace, id_attr->value, MAX_TOKEN_LEN);

    Key_Value *entry = put_symbol(&lexer->symbols, subprocess_namespace, symbol);

    Attr *name = get_attr(attrs, "name");
    if (name) {
        memcpy(entry->value.as.subprocess.name, name->value, MAX_TOKEN_LEN);
    }

    parse_events(lexer, screen, subprocess_namespace);

    Screen_Object subprocess_obj = {
        .value = &entry->value,
        .rect = {
            .width = screen->settings.sub_width,
            .height = screen->settings.sub_height,
            .x = 0,
            .y = screen->rows
        },
    };

    screen->rows += screen->settings.rows_per_sub;
    entry->value.obj_id = push_obj(screen, subprocess_obj);

    assert_next_token(lexer, TOKEN_OPTAG);
    assert_next_token(lexer, TOKEN_SLASH);
    assert_next_token(lexer, TOKEN_SUBPROCESS);
    assert_next_token(lexer, TOKEN_CLTAG);
}

void parse_events(Lexer *lexer, Screen *screen, char *namespace) {
    assert_next_token(lexer, TOKEN_OPTAG);
    assert_next_token(lexer, TOKEN_EVENTS);
    assert_next_token(lexer, TOKEN_CLTAG);

    int col = 0;
    for (;;) {
        assert_next_token(lexer, TOKEN_OPTAG);
        next_token_fail_if_eof(lexer);
        if (lexer->token.kind == TOKEN_SLASH) {
            next_token_fail_if_eof(lexer);
            if (lexer->token.kind == TOKEN_EVENTS) {
                assert_next_token(lexer, TOKEN_CLTAG);
                break;
            }

            PRINT_ERROR_FMT(lexer, "Unexpected closing tag %s. Perhaps you want to close `events`?", lexer->token.value);
            FAIL;
        }

        if (lexer->token.kind == TOKEN_TYPE) {
            parse_event(lexer, screen, col++, namespace);
        } else if (lexer->token.kind == TOKEN_COL) {
            parse_columns(lexer, screen, col++, namespace);
        } else {
            PRINT_ERROR_FMT(lexer, "Unexpected tag `<%s`", lexer->token.value);
            FAIL;
        }
    }
}

void parse_columns(Lexer *lexer, Screen *screen, int cur_col, char *namespace) {
    next_token_fail_if_eof(lexer);
    if (lexer->token.kind == TOKEN_SLASH) {
        assert_next_token(lexer, TOKEN_CLTAG);
        return;
    }

    if (lexer->token.kind != TOKEN_CLTAG) {
        PRINT_ERROR(lexer, "Syntax error");
        FAIL;
    }

    int count = 0;
    for (;;) {
        assert_next_token(lexer, TOKEN_OPTAG);
        next_token_fail_if_eof(lexer);
        if (lexer->token.kind == TOKEN_SLASH) {
            next_token_fail_if_eof(lexer);
            if (lexer->token.kind == TOKEN_COL) {
                assert_next_token(lexer, TOKEN_CLTAG);
                break;
            }

            PRINT_ERROR_FMT(lexer, "Unexpected closing tag %s. Perhaps you want to close `col`?", lexer->token.value);
            FAIL;
        }

        if (count >= 3) {
            PRINT_ERROR(lexer, "`col` tag can have at must 3 events");
            FAIL;
        }

        if (lexer->token.kind == TOKEN_TYPE) {
            parse_event(lexer, screen, cur_col, namespace);
        } else {
            PRINT_ERROR_FMT(lexer, "Unexpected tag `<%s`", lexer->token.value);
            FAIL;
        }

        count++;
    }
}

void parse_event(Lexer *lexer, Screen *screen, int col, char *namespace) {
    ASSERT(lexer->token.kind == TOKEN_TYPE && "Invalid event token");
    char buffer[MAX_TOKEN_LEN];

    Event_Kind event_kind = translate_event(lexer->token.value);
    if (event_kind == EVENT_INVALID) {
        PRINT_ERROR_FMT(lexer, "Invalid event type `%s`", lexer->token.value);
        FAIL;
    }

    Attr_List attrs = {0};
    parse_attrs(lexer, &attrs);

    Attr *id_attr = get_attr(attrs, "id");
    if (id_attr == NULL) {
        PRINT_ERROR(lexer, "Event need to have an `id`");
        FAIL;
    }


    Symbol symbol = {0};
    symbol.as.event.kind = event_kind;
    symbol.kind = SYMB_EVENT;

    symb_name(buffer, namespace, id_attr->value);
    Key_Value *kv = put_symbol(&lexer->symbols, buffer, symbol);

    Screen_Object obj;
    switch (event_kind) {
        case EVENT_TASK:    obj = parse_event_task(lexer, attrs, kv, screen, col, namespace);    break;
        case EVENT_STARTER: obj = parse_event_starter(lexer, attrs, kv, screen, col, namespace); break;
        case EVENT_WAIT:
        case EVENT_MAIL:
            obj = parse_event_with_sprite(lexer, attrs, kv, screen, col, namespace);             break;
        case EVENT_GATEWAY: obj = parse_event_gateway(attrs, kv, screen, col, namespace);        break;
        case EVENT_END:     obj = parse_event_end(screen, col);                                  break;
        default: ASSERT(0 && "Unreachable statement");
    }

    obj.value = &kv->value;
    kv->value.obj_id = push_obj(screen, obj);
}

void parse_attrs(Lexer *lexer, Attr_List *attrs) {
    while (attrs->len < MAX_ATTRS) {
        next_token_fail_if_eof(lexer);
        if (lexer->token.kind == TOKEN_SLASH) {
            assert_next_token(lexer, TOKEN_CLTAG);
            break;
        }

        if (lexer->token.kind == TOKEN_CLTAG) {
            break;
        }

        if (lexer->token.kind != TOKEN_ID) {
            PRINT_ERROR_FMT(lexer, "Invalid token %s", lexer->token.value);
            FAIL;
        }

        Attr *new_attr = &attrs->items[attrs->len++];

        memcpy(new_attr->id, lexer->token.value, MAX_TOKEN_LEN);

        assert_next_token(lexer, TOKEN_ATR);
        assert_next_token(lexer, TOKEN_STR);

        memcpy(new_attr->value, lexer->token.value, MAX_TOKEN_LEN);
    }
}

Screen_Object parse_event_task(Lexer *lexer, Attr_List attrs, Key_Value *symbol, Screen *screen, int col, char *namespace) {
    char buffer[MAX_TOKEN_LEN];
    int row_number = 1;

    Attr *name = get_attr(attrs, "name");
    if (name) {
        memcpy(symbol->value.as.event.title, name->value, MAX_TOKEN_LEN);
    }

    Attr *points = get_attr(attrs, "points");
    if (points) {
        symb_name(buffer, namespace, points->value);
        memcpy(symbol->value.as.event.points_to, buffer, MAX_TOKEN_LEN);
    }

    Attr *row = get_attr(attrs, "row");
    if (row) {
        row_number = translate_row(lexer, row->value);
    }

    return (Screen_Object) {
        .rect = {
            .height = 90,
            .width = 100,
            .x = col,
            .y = screen->rows + row_number
        }
    };
}

Screen_Object parse_event_starter(Lexer *lexer, Attr_List attrs, Key_Value *symbol, Screen *screen, int col, char *namespace) {
    char buffer[MAX_TOKEN_LEN];
    int row_number = 1;

    Attr *points = get_attr(attrs, "points");
    if (points) {
        symb_name(buffer, namespace, points->value);
        memcpy(symbol->value.as.event.points_to, buffer, MAX_TOKEN_LEN);
    }

    Attr *row = get_attr(attrs, "row");
    if (row) {
        row_number = translate_row(lexer, row->value);
    }

    return (Screen_Object) {
        .rect = {
            .height = 40,
            .width = 40,
            .x = col,
            .y = screen->rows + row_number
        },
    };
}

Screen_Object parse_event_with_sprite(Lexer *lexer, Attr_List attrs, Key_Value *symbol, Screen *screen, int col, char *namespace) {
    char buffer[MAX_TOKEN_LEN];
    int row_number = 1;

    Attr *points = get_attr(attrs, "points");
    if (points) {
        symb_name(buffer, namespace, points->value);
        memcpy(symbol->value.as.event.points_to, buffer, MAX_TOKEN_LEN);
    }

    Attr *row = get_attr(attrs, "row");
    if (row) {
        row_number = translate_row(lexer, row->value);
    }

    return (Screen_Object) {
        .rect = {
            .height = 64,
            .width = 64,
            .x = col,
            .y = screen->rows + row_number
        },
    };
}

Screen_Object parse_event_gateway(Attr_List attrs, Key_Value *symbol, Screen *screen, int col, char *namespace) {
    char buffer[MAX_TOKEN_LEN];
    int row_number = 1;

    Attr *points = get_attr(attrs, "points");
    if (points) {
        int len = 0;
        const char **words = TextSplit(points->value, ',', &len);
        for (int i = 0; i < len && i < 3; i++) {
            symb_name(buffer, namespace, words[i]);
            memcpy(symbol->value.as.event.points_to[i], buffer, MAX_TOKEN_LEN);
        }
    }

    return (Screen_Object) {
        .rect = {
            .height = 32,
            .width = 32,
            .x = col,
            .y = screen->rows + row_number
        },
    };
}

Screen_Object parse_event_end(Screen *screen, int col) {
    return (Screen_Object) {
        .rect = {
            .height = 40,
            .width = 40,
            .x = col,
            .y = screen->rows + 1
        }
    };
}


Event_Kind translate_event(const char *event) {
    if (strcmp(event, "starter") == 0) {
        return EVENT_STARTER;
    }

    if (strcmp(event, "wait") == 0) {
        return EVENT_WAIT;
    }

    if (strcmp(event, "mail") == 0) {
        return EVENT_MAIL;
    }

    if (strcmp(event, "task") == 0) {
        return EVENT_TASK;
    }

    if (strcmp(event, "gateway") == 0) {
        return EVENT_GATEWAY;
    }

    if (strcmp(event, "end") == 0) {
        return EVENT_END;
    }

    return EVENT_INVALID;
}

int translate_row(Lexer *lexer, const char *row) {
    if (strncmp(row, "up", 3) == 0) {
        return 0;
    }

    if (strncmp(row, "mid", 4) == 0) {
        return 1;
    }

    if (strncmp(row, "down", 5) == 0) {
        return 2;
    }

    PRINT_ERROR_FMT(lexer, "Invalid row name `%s`. Expected values: up, mid, down", row);
    FAIL;
}

void load_resources(Screen *screen) {
    screen->font = LoadFontFromMemory(".ttf", resources[RESOURCE_FONT_RUBIK].data, resources[RESOURCE_FONT_RUBIK].size, screen->settings.font_size, NULL, 0);
    screen->font_header = LoadFontFromMemory(".ttf", resources[RESOURCE_FONT_RUBIK].data, resources[RESOURCE_FONT].size, screen->settings.font_size_header, NULL, 0);

    Image mail = LoadImageFromMemory(".png", resources[RESOURCE_EMAIL].data, resources[RESOURCE_EMAIL].size);
    screen->mail_texture = LoadTextureFromImage(mail);

    Image relogio = LoadImageFromMemory(".png", resources[RESOURCE_RELOGIO].data, resources[RESOURCE_RELOGIO].size);
    screen->wait_texture = LoadTextureFromImage(relogio);

    Image rect = LoadImageFromMemory(".png", resources[RESOURCE_RECTANGLE].data, resources[RESOURCE_RECTANGLE].size);
    screen->rect_rexture = LoadTextureFromImage(rect);

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

    parse(&lexer, &screen);
    setup_screen(&screen);

    InitWindow(screen.settings.width, screen.settings.height + screen.settings.header_height, screen.title);

    load_resources(&screen);


    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(WHITE);

        // DrawTextureEx(email_texture, VECTOR(50, 50), 0, 1, WHITE);
        // DrawTextureRec(email_texture, )


        draw_header(screen);
        for (size_t i = 0; i < screen.objs_cnt; i++) {
            Screen_Object obj = screen.screen_objects[i];
            if (obj.value->kind == SYMB_EVENT) {
                for (size_t j = 0; j < 3; j++) {
                    Key_Value *to = get_symbol(&lexer.symbols, obj.value->as.event.points_to[j]);
                    if (to != NULL) {
                        draw_arrow(screen, obj, screen.screen_objects[to->value.obj_id]);
                    }
                }
            }
        }

        for (size_t i = 0; i < screen.objs_cnt; i++) {
            draw_obj(screen, screen.screen_objects[i]);
        }

        EndDrawing();
    }

    CloseWindow();

    return EXIT_SUCCESS;
}

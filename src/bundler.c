#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

char *RESOURCE_NAMES_AND_PATHS[][2] = {
    { "FONT", "./resources/font.ttf"         },
    { "FONT_RUBIK", "./resources/font_2.ttf" },
    { "EMAIL", "./resources/email.png"       },
    { "RELOGIO", "./resources/relogio.png"   },
};

#define BANDLE_FILE_PATH "./src/bundle.c"

FILE *OUT;

char *read_file(const char *file_path, size_t *file_size) {
    char *content = NULL;
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        printf("Cannot open file %s\n", file_path);
        goto CLEAN_UP;
    }

    if (fseek(file, 0L, SEEK_END) != 0) goto CLEAN_UP;

    *file_size = ftell(file);
    content = malloc(sizeof(char)*(*file_size));
    rewind(file);

    size_t bytes_readed = fread(content, sizeof(char), (*file_size), file);
    if (bytes_readed != (*file_size)) {
        printf("Cannot read full file\n");
        printf("File size: %ld\n", (*file_size));
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

#define FGENERATE(fmt, ...) fprintf(OUT, fmt, __VA_ARGS__)
#define GENERATE(line) fprintf(OUT, line)

void generate_bytes(char *resource_name, char *resource) {
    FGENERATE("const unsigned char __data_%s[] = {", resource_name);
    size_t size = 0;

    const char *bytes = read_file(resource, &size);
    for (size_t i = 0; i < size; i++) {
        if ((i%20) == 0) {
            GENERATE("\n    ");
        }
        FGENERATE("0x%02hhX,", bytes[i]);
    }

    GENERATE("\n};\n\n");
    FGENERATE("const Resource __resource_%s = { .data = __data_%s, .size = %ld };\n\n", resource_name, resource_name, size);

}

void generate_resource(char *resource_name) {
    FGENERATE("    __resource_%s,\n", resource_name);
}

int main(void) {
    OUT = fopen(BANDLE_FILE_PATH, "w+");
    if (OUT == NULL) {
        printf("Cannot open/create %s\n", BANDLE_FILE_PATH);
        return 1;
    }

    GENERATE(
    "typedef struct {\n"
    "    const unsigned char *data;\n"
    "    size_t size;\n"
    "} Resource;\n\n"
    );

    size_t total_resources = sizeof(RESOURCE_NAMES_AND_PATHS) / sizeof(RESOURCE_NAMES_AND_PATHS[0]);
    for (size_t i = 0; i < total_resources; i++) {
        generate_bytes(RESOURCE_NAMES_AND_PATHS[i][0], RESOURCE_NAMES_AND_PATHS[i][1]);
        FGENERATE("#define RESOURCE_%s %ld\n", RESOURCE_NAMES_AND_PATHS[i][0], i);
    }

    GENERATE("\nResource resources[] = {\n");
    for (size_t i = 0; i < total_resources; i++) {
        generate_resource(RESOURCE_NAMES_AND_PATHS[i][0]);
    }
    GENERATE("};\n\n");

    return 0;
}

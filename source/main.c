#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "memory.c"
#include "lexer.c"

static bool read_entire_stdin(char **out_buffer, size_t *out_length) {
    size_t capacity = 1024;
    char *buffer = allocate_or_abort(capacity);

    size_t bytes_read = 0;
    for (;;) {
        if (bytes_read >= capacity) {
            capacity *= 2;
            buffer = reallocate_or_abort(buffer, capacity);
        }

        size_t bytes_read_now = fread(buffer + bytes_read, 1, capacity - bytes_read, stdin);
        bytes_read += bytes_read_now;
        if (bytes_read_now == 0) {
            if (ferror(stdin)) {
                fprintf(stderr, "ERROR: Failed to read from stdin\n");
                free(buffer);
                return false;
            }
            
            break;
        }
    }

    *out_buffer = buffer;
    *out_length = bytes_read;
    return true;
}

static bool read_entire_file(const char *path, char **out_buffer, size_t *out_length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || ferror(file)) {
        fprintf(stderr, "ERROR: Failed to open file %s.\n", path);
        return false;
    }

    long length = 0;
    if (fseek(file, 0, SEEK_END) < 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) < 0) {
        fprintf(stderr, "ERROR: Failed to count the length of file %s.\n", path);
        fclose(file);
        return false;
    }
    size_t ulength = (size_t)length;
    
    char *buffer = allocate_or_abort(ulength);
    if (fread(buffer, 1, ulength, file) != ulength) {
        fprintf(stderr, "ERROR: Failed to read %zu bytes from %s.\n", length, path);
        fclose(file);
        free(buffer);
        return false;
    }

    fclose(file);
    *out_buffer = buffer;
    *out_length = ulength;
    return true;
}

static bool read_entire_input_data(int argc, char **argv, char **out_buffer, size_t *out_length) {
    const char *program_name = argv[0];
    
    if (argc == 1) {
        if (isatty(STDIN_FILENO)) {
            fprintf(stderr, "ERROR: No input is provided.\n");
        } else {
            return read_entire_stdin(out_buffer, out_length);
        }
    } else if (argc == 2) {
        return read_entire_file(argv[1], out_buffer, out_length);
    }

    fprintf(stderr, "USAGE: %s input.sql\n", program_name);
    return false;
}

int main(int argc, char **argv) {
    char *input = NULL;
    size_t length = 0;
    if (!read_entire_input_data(argc, argv, &input, &length)) {
        return EXIT_FAILURE;
    }

    Lexer lexer = lexer_create(argv[argc - 1], input, length);
    lexer_tokenize(&lexer);

    lexer_print_reports(&lexer, stdout);

    printf("Number of recognized tokens: x%zu\n", lexer.tokens.count);
    for (size_t i = 0; i < lexer.tokens.count; ++i) {
        char buffer[1024];
        token_to_string((Token *)array_at(&lexer.tokens, i), buffer, sizeof buffer);
        printf("- Token #%zu: %.*s\n", i + 1, (int)(sizeof buffer), buffer);
    }

    bool errors = lexer.reporter.errors >= 1;
    lexer_destroy(&lexer);
    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

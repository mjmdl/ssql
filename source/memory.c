static void *allocate_or_abort(size_t size) {
    void *address = malloc(size);
    if (address == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate %zu bytes of memory.\n", size);
        abort();
    }
    return address;
}

static void *reallocate_or_abort(void *old_address, size_t new_size) {
    void *new_address = realloc(old_address, new_size);
    if (new_address == NULL) {
        fprintf(stderr, "ERROR: Failed to reallocate memory at %p with %zu bytes of memory.\n", old_address, new_size);
        abort();
    }
    return new_address;
}

static size_t get_aligned_offset(size_t offset, size_t stride) {
    assert((stride != 0) && ((stride & (stride - 1)) == 0));
    return (-offset) & (stride - 1);
}

typedef struct Array {
    void *elements;
    size_t capacity;
    size_t count;
    size_t stride;
} Array;

static Array array_create(size_t capacity, size_t stride) {
    return (Array){
        .elements = capacity == 0 ? NULL : allocate_or_abort(capacity * stride),
        .capacity = capacity,
        .count = 0,
        .stride = stride,
    };
}

static void array_destroy(Array *array) {
    if (array->elements != NULL) {
        free(array->elements);
    }
    *array = (Array){0};
}

static void *array_allocate(Array *array) {
    if (array->count >= array->capacity) {
        array->capacity = array->capacity == 0 ? 2 : array->capacity * 2;
        array->elements = reallocate_or_abort(array->elements, array->capacity * array->stride);
    }
    void *address = ((uint8_t *)array->elements) + (array->count * array->stride);
    array->count += 1;
    return address;
}

static void *array_at(Array *array, size_t index) {
    assert(index < array->count && array->elements != NULL);
    return (uint8_t *)array->elements + (index * array->stride);
}

typedef struct Arena_Block {
    struct Arena_Block *next;
    size_t capacity;
    size_t count;
    uint8_t data[];
} Arena_Block;

typedef struct Arena {
    Arena_Block *block;
} Arena;

static Arena arena_create(size_t capacity) {
    Arena_Block *block = allocate_or_abort(sizeof *block + capacity);
    *block = (Arena_Block){
        .next = NULL,
        .capacity = capacity,
        .count = 0,
    };
    return (Arena){
        .block = block,
    };
}

static void arena_block_destroy(Arena_Block *block) {
    if (block->next != NULL) {
        arena_block_destroy(block->next);
    }
    free(block);
}

static void arena_destroy(Arena *arena) {
    if (arena->block != NULL) {
        arena_block_destroy(arena->block);
    }
    *arena = (Arena){0};
}

static void *arena_allocate(Arena *arena, size_t size, size_t alignment) {
    Arena_Block *block = arena->block;
    size_t offset = get_aligned_offset(block->count, alignment);
    if (block != NULL) {
        for (;;) {
            size_t space = block->capacity - block->count - offset;
            if (size <= space) {
                break;
            }
            if (block->next == NULL) {
                block->next = arena_create(size > arena->block->capacity ? size : arena->block->capacity).block;
                block = block->next;
                break;
            }
            block = block->next;
            offset = get_aligned_offset(block->count, alignment);
        }
    } else {
        block = arena_create(size).block;
        arena->block = block;
    }
    void *address = arena->block->data + arena->block->count + offset;
    arena->block->count += size + offset;
    return address;
}

static char *arena_duplicate_string(Arena *arena, const char *string, size_t length) {
    char *clone = arena_allocate(arena, length + 1, sizeof *string);
    for (size_t i = 0; i < length; i += 1) {
        clone[i] = string[i];
    }
    clone[length] = '\0';
    return clone;
}

#pragma once
#include "defines.hpp"

typedef enum memory_tag {
    MEMORY_TAG_UNKNOWN = 0,
    MEMORY_TAG_ARRAY,
    MEMORY_TAG_LINEAR_ALLOCATOR,
    MEMORY_TAG_DARRAY,
    MEMORY_TAG_DICT,
    MEMORY_TAG_RING_QUEUE,
    MEMORY_TAG_BST,
    MEMORY_TAG_STRING,
    MEMORY_TAG_APPLICATION,
    MEMORY_TAG_JOB,
    MEMORY_TAG_TEXTURE,
    MEMORY_TAG_MATERIAL_INSTANCE,
    MEMORY_TAG_RENDERER,
    MEMORY_TAG_GAME,
    MEMORY_TAG_TRANSFORM,
    MEMORY_TAG_ENTITY,
    MEMORY_TAG_ENTITY_NODE,
    MEMORY_TAG_SCENE,
    MEMORY_TAG_MAX_TAGS
} memory_tag;

TAK_API void memory_init(void);
TAK_API void memory_shutdown(void);   // leak check + summary
TAK_API void* memory_alloc (u32 size, memory_tag tag);
TAK_API void  memory_free  (void* block, u32 size, memory_tag tag);
TAK_API u32 memory_bytes(memory_tag tag);   
TAK_API u32 memory_allocs(memory_tag tag); 
TAK_API void memory_log(void);
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct elmd_svg_normalizer elmd_svg_normalizer;

elmd_svg_normalizer* elmd_svg_normalizer_create(void);
void elmd_svg_normalizer_destroy(elmd_svg_normalizer* normalizer);
int32_t elmd_svg_normalize(elmd_svg_normalizer* normalizer, const uint8_t* input, size_t input_len, float font_size, uint8_t** output, size_t* output_len);
void elmd_svg_buffer_destroy(uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

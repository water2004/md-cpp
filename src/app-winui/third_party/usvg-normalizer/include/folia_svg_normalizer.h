#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct folia_svg_normalizer folia_svg_normalizer;

folia_svg_normalizer* folia_svg_normalizer_create(void);
void folia_svg_normalizer_destroy(folia_svg_normalizer* normalizer);
int32_t folia_svg_normalize(folia_svg_normalizer* normalizer, const uint8_t* input, size_t input_len, float font_size, uint8_t** output, size_t* output_len, float* width, float* height);
void folia_svg_buffer_destroy(uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

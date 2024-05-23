/*
 * images.h
 *
 *  Created on: May 23, 2024
 *      Author: dziam
 */
#pragma once

#include <stdint.h>
#include "hagl.h"

extern const uint8_t image_intro[];
extern const size_t image_intro_size;

void init_bitmap(bitmap_t *bitmap, const uint8_t *image, size_t size, uint16_t width, uint16_t height);



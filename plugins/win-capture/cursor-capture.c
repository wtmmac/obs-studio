#include <windows.h>
#include <obs.h>
#include "cursor-capture.h"

static uint8_t *get_bitmap_data(HBITMAP hbmp, BITMAP *bmp)
{
	if (GetObject(hbmp, sizeof(*bmp), bmp) != 0) {
		uint8_t *output;
		unsigned int size =
			(bmp->bmHeight * bmp->bmWidth * bmp->bmBitsPixel) / 8;

		output = bmalloc(size);
		GetBitmapBits(hbmp, size, output);
		return output;
	}

	return NULL;
}

static inline uint8_t bit_to_alpha(uint8_t *data, long pixel, bool invert)
{
	uint8_t pix_byte = data[pixel / 8];
	bool alpha = (pix_byte >> (7 - pixel % 7) & 1) != 0;

	if (invert) {
		return alpha ? 0xFF : 0;
	} else {
		return alpha ? 0 : 0xFF;
	}
}

static inline bool bitmap_has_alpha(uint8_t *data, long num_pixels)
{
	for (long i = 0; i < num_pixels; i++) {
		if (data[i * 4 + 3] != 0) {
			return true;
		}
	}

	return false;
}

static inline void apply_mask(uint8_t *color, uint8_t *mask, long num_pixels)
{
	for (long i = 0; i < num_pixels; i++)
		color[i * 4 + 3] = bit_to_alpha(mask, i, false);
}

static inline uint8_t *copy_from_color(ICONINFO *ii, uint32_t *width,
		uint32_t *height)
{
	BITMAP bmp_color;
	BITMAP bmp_mask;
	uint8_t *color;
	uint8_t *mask;

	color = get_bitmap_data(ii->hbmColor, &bmp_color);
	if (!color) {
		return NULL;
	}

	if (bmp_color.bmBitsPixel < 32) {
		bfree(color);
		return NULL;
	}

	mask = get_bitmap_data(ii->hbmMask, &bmp_mask);
	if (mask) {
		long pixels = bmp_color.bmHeight * bmp_color.bmWidth;

		if (!bitmap_has_alpha(color, pixels))
			apply_mask(color, mask, pixels);

		bfree(mask);
	}

	*width = bmp_color.bmWidth;
	*height = bmp_color.bmHeight;
	return color;
}

static inline uint8_t *copy_from_mask(ICONINFO *ii, uint32_t *width,
		uint32_t *height)
{
	uint8_t *output;
	uint8_t *mask;
	long pixels;
	long bottom;
	BITMAP bmp;

	mask = get_bitmap_data(ii->hbmMask, &bmp);
	if (!mask) {
		return NULL;
	}

	bmp.bmHeight /= 2;

	pixels = bmp.bmHeight * bmp.bmWidth;
	output = bzalloc(pixels * 4);

	bottom = bmp.bmWidthBytes * bmp.bmHeight;

	for (long i = 0; i < pixels; i++) {
		uint8_t alpha = bit_to_alpha(mask, i, false);
		uint8_t color = bit_to_alpha(mask + bottom, i, true);

		if (!alpha) {
			output[i * 4 + 3] = color;
		} else {
			*(uint32_t*)&output[i * 4] = !!color ?
				0xFFFFFFFF : 0xFF000000;
		}
	}

	bfree(mask);

	*width = bmp.bmWidth;
	*height = bmp.bmHeight;
	return output;
}

static inline uint8_t *cursor_capture_icon_bitmap(ICONINFO *ii,
		uint32_t *width, uint32_t *height)
{
	uint8_t *output;

	output = copy_from_color(ii, width, height);
	if (!output)
		output = copy_from_mask(ii, width, height);

	return output;
}

static inline bool cursor_capture_icon(struct cursor_data *data, HICON icon)
{
	bool success = false;
	uint8_t *bitmap;
	uint32_t height;
	uint32_t width;
	ICONINFO ii;

	gs_texture_destroy(data->texture);
	data->texture = NULL;

	if (!icon) {
		return false;
	}
	if (!GetIconInfo(icon, &ii)) {
		return false;
	}

	bitmap = cursor_capture_icon_bitmap(&ii, &width, &height);
	if (bitmap) {
		data->texture = gs_texture_create(width, height, GS_BGRA,
				1, &bitmap, 0);
		bfree(bitmap);
	}

	DeleteObject(ii.hbmColor);
	DeleteObject(ii.hbmMask);
	return !!data->texture;
}

void cursor_capture(struct cursor_data *data)
{
	CURSORINFO ci = {0};
	HICON icon;

	ci.cbSize = sizeof(ci);

	if (!GetCursorInfo(&ci)) {
		data->visible = false;
		return;
	}

	memcpy(&data->cursor_pos, &ci.ptScreenPos, sizeof(data->cursor_pos));

	if (data->current_cursor == ci.hCursor) {
		return;
	}

	icon = CopyIcon(ci.hCursor);
	data->visible = cursor_capture_icon(data, icon);
	data->current_cursor = ci.hCursor;
	if ((ci.flags & CURSOR_SHOWING) == 0)
		data->visible = false;
	DestroyIcon(icon);
}

void cursor_draw(struct cursor_data *data, long x_offset, long y_offset,
		float x_scale, float y_scale)
{
	long x = data->cursor_pos.x + x_offset - data->x_hotspot;
	long y = data->cursor_pos.y + y_offset - data->y_hotspot;

	if (data->visible && !!data->texture) {
		gs_matrix_push();
		gs_matrix_scale3f(x_scale, y_scale, 1.0f);
		obs_source_draw(data->texture, x, y, 0, 0, false);
		gs_matrix_pop();
	}
}

void cursor_data_free(struct cursor_data *data)
{
	gs_texture_destroy(data->texture);
	memset(data, 0, sizeof(*data));
}

/*
** r_drawt.cpp
** Faster column drawers for modern processors
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
** These functions stretch columns into a temporary buffer and then
** map them to the screen. On modern machines, this is faster than drawing
** them directly to the screen.
**
** Will I be able to even understand any of this if I come back to it later?
** Let's hope so. :-)
*/

#include "templates.h"
#include "doomtype.h"
#include "doomdef.h"
#include "r_defs.h"
#include "r_draw.h"
#include "r_main.h"
#include "r_things.h"
#include "v_video.h"

// I should have commented this stuff better.
//
// dc_temp is the buffer R_DrawColumnHoriz writes into.
// dc_tspans points into it.
// dc_ctspan points into dc_tspans.
// horizspan also points into dc_tspans.

// dc_ctspan is advanced while drawing into dc_temp.
// horizspan is advanced up to dc_ctspan when drawing from dc_temp to the screen.

canvas_pixel_t dc_tempbuff[MAXHEIGHT*4];
canvas_pixel_t *dc_temp;
unsigned int dc_tspans[4][MAXHEIGHT];
unsigned int *dc_ctspan[4];
unsigned int *horizspan[4];

#ifdef X86_ASM
extern "C" void R_SetupShadedCol();
extern "C" void R_SetupAddCol();
extern "C" void R_SetupAddClampCol();
#endif

#ifndef X86_ASM
// Copies one span at hx to the screen at sx.
void rt_copy1col_c (int hx, int sx, int yl, int yh)
{
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch;

	if (count & 1) {
		*dest = *source;
		source += 4;
		dest += pitch;
	}
	if (count & 2) {
		dest[0] = source[0];
		dest[pitch] = source[4];
		source += 8;
		dest += pitch*2;
	}
	if (!(count >>= 2))
		return;

	do {
		dest[0] = source[0];
		dest[pitch] = source[4];
		dest[pitch*2] = source[8];
		dest[pitch*3] = source[12];
		source += 16;
		dest += pitch*4;
	} while (--count);
}

// Copies all four spans to the screen starting at sx.
void rt_copy4cols_c (int sx, int yl, int yh)
{
#ifndef PALETTEOUTPUT
	// To do: we could do this with SSE using __m128i
	rt_copy1col_c(0, sx, yl, yh);
	rt_copy1col_c(1, sx + 1, yl, yh);
	rt_copy1col_c(2, sx + 2, yl, yh);
	rt_copy1col_c(3, sx + 3, yl, yh);
#else
	int *source;
	int *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = (int *)(ylookup[yl] + sx + dc_destorg);
	source = (int *)(&dc_temp[yl*4]);
	pitch = dc_pitch/sizeof(int);
	
	if (count & 1) {
		*dest = *source;
		source += 4/sizeof(int);
		dest += pitch;
	}
	if (!(count >>= 1))
		return;

	do {
		dest[0] = source[0];
		dest[pitch] = source[4/sizeof(int)];
		source += 8/sizeof(int);
		dest += pitch*2;
	} while (--count);
#endif
}

// Maps one span at hx to the screen at sx.
void rt_map1col_c (int hx, int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);
#endif

	colormap = dc_colormap;
	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch;

	if (count & 1) {
#ifndef PALETTEOUTPUT
		*dest = shade_pal_index(colormap[*source], light);
#else
		*dest = colormap[*source];
#endif
		source += 4;
		dest += pitch;
	}
	if (!(count >>= 1))
		return;

	do {
#ifndef PALETTEOUTPUT
		dest[0] = shade_pal_index(colormap[source[0]], light);
		dest[pitch] = shade_pal_index(colormap[source[4]], light);
#else
		dest[0] = colormap[source[0]];
		dest[pitch] = colormap[source[4]];
#endif
		source += 8;
		dest += pitch*2;
	} while (--count);
}

// Maps all four spans to the screen starting at sx.
void rt_map4cols_c (int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);
#endif

	colormap = dc_colormap;
	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4];
	pitch = dc_pitch;
	
	if (count & 1) {
#ifndef PALETTEOUTPUT
		dest[0] = shade_pal_index(colormap[source[0]], light);
		dest[1] = shade_pal_index(colormap[source[1]], light);
		dest[2] = shade_pal_index(colormap[source[2]], light);
		dest[3] = shade_pal_index(colormap[source[3]], light);
#else
		dest[0] = colormap[source[0]];
		dest[1] = colormap[source[1]];
		dest[2] = colormap[source[2]];
		dest[3] = colormap[source[3]];
#endif
		source += 4;
		dest += pitch;
	}
	if (!(count >>= 1))
		return;

	do {
#ifndef PALETTEOUTPUT
		dest[0] = shade_pal_index(colormap[source[0]], light);
		dest[1] = shade_pal_index(colormap[source[1]], light);
		dest[2] = shade_pal_index(colormap[source[2]], light);
		dest[3] = shade_pal_index(colormap[source[3]], light);
		dest[pitch] = shade_pal_index(colormap[source[4]], light);
		dest[pitch + 1] = shade_pal_index(colormap[source[5]], light);
		dest[pitch + 2] = shade_pal_index(colormap[source[6]], light);
		dest[pitch + 3] = shade_pal_index(colormap[source[7]], light);
#else
		dest[0] = colormap[source[0]];
		dest[1] = colormap[source[1]];
		dest[2] = colormap[source[2]];
		dest[3] = colormap[source[3]];
		dest[pitch] = colormap[source[4]];
		dest[pitch+1] = colormap[source[5]];
		dest[pitch+2] = colormap[source[6]];
		dest[pitch+3] = colormap[source[7]];
#endif
		source += 8;
		dest += pitch*2;
	} while (--count);
}
#endif

void rt_Translate1col(const BYTE *translation, int hx, int yl, int yh)
{
	int count = yh - yl + 1;
	canvas_pixel_t *source = &dc_temp[yl*4 + hx];

	// Things we do to hit the compiler's optimizer with a clue bat:
	// 1. Parallelism is explicitly spelled out by using a separate
	//    C instruction for each assembly instruction. GCC lets me
	//    have four temporaries, but VC++ spills to the stack with
	//    more than two. Two is probably optimal, anyway.
	// 2. The results of the translation lookups are explicitly
	//    stored in byte-sized variables. This causes the VC++ code
	//    to use byte mov instructions in most cases; for apparently
	//    random reasons, it will use movzx for some places. GCC
	//    ignores this and uses movzx always.

	// Do 8 rows at a time.
	for (int count8 = count >> 3; count8; --count8)
	{
		int c0, c1;
		BYTE b0, b1;

		c0 = source[0];			c1 = source[4];
		b0 = translation[c0];	b1 = translation[c1];
		source[0] = b0;			source[4] = b1;

		c0 = source[8];			c1 = source[12];
		b0 = translation[c0];	b1 = translation[c1];
		source[8] = b0;			source[12] = b1;

		c0 = source[16];		c1 = source[20];
		b0 = translation[c0];	b1 = translation[c1];
		source[16] = b0;		source[20] = b1;

		c0 = source[24];		c1 = source[28];
		b0 = translation[c0];	b1 = translation[c1];
		source[24] = b0;		source[28] = b1;

		source += 32;
	}
	// Finish by doing 1 row at a time.
	for (count &= 7; count; --count, source += 4)
	{
		source[0] = translation[source[0]];
	}
}

void rt_Translate4cols(const BYTE *translation, int yl, int yh)
{
	int count = yh - yl + 1;
	canvas_pixel_t *source = &dc_temp[yl*4];
	int c0, c1;
	BYTE b0, b1;

	// Do 2 rows at a time.
	for (int count8 = count >> 1; count8; --count8)
	{
		c0 = source[0];			c1 = source[1];
		b0 = translation[c0];	b1 = translation[c1];
		source[0] = b0;			source[1] = b1;

		c0 = source[2];			c1 = source[3];
		b0 = translation[c0];	b1 = translation[c1];
		source[2] = b0;			source[3] = b1;

		c0 = source[4];			c1 = source[5];
		b0 = translation[c0];	b1 = translation[c1];
		source[4] = b0;			source[5] = b1;

		c0 = source[6];			c1 = source[7];
		b0 = translation[c0];	b1 = translation[c1];
		source[6] = b0;			source[7] = b1;

		source += 8;
	}
	// Do the final row if count was odd.
	if (count & 1)
	{
		c0 = source[0];			c1 = source[1];
		b0 = translation[c0];	b1 = translation[c1];
		source[0] = b0;			source[1] = b1;

		c0 = source[2];			c1 = source[3];
		b0 = translation[c0];	b1 = translation[c1];
		source[2] = b0;			source[3] = b1;
	}
}

// Translates one span at hx to the screen at sx.
void rt_tlate1col (int hx, int sx, int yl, int yh)
{
	rt_Translate1col(dc_translation, hx, yl, yh);
	rt_map1col(hx, sx, yl, yh);
}

// Translates all four spans to the screen starting at sx.
void rt_tlate4cols (int sx, int yl, int yh)
{
	rt_Translate4cols(dc_translation, yl, yh);
	rt_map4cols(sx, yl, yh);
}

// Adds one span at hx to the screen at sx without clamping.
void rt_add1col (int hx, int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch;
	colormap = dc_colormap;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);

	do {
		uint32_t fg = shade_pal_index(colormap[*source], light);
		uint32_t fg_red = (fg >> 16) & 0xff;
		uint32_t fg_green = (fg >> 8) & 0xff;
		uint32_t fg_blue = fg & 0xff;

		uint32_t bg_red = (*dest >> 16) & 0xff;
		uint32_t bg_green = (*dest >> 8) & 0xff;
		uint32_t bg_blue = (*dest) & 0xff;

		uint32_t red = clamp<uint32_t>(fg_red + bg_red, 0, 255);
		uint32_t green = clamp<uint32_t>(fg_green + bg_green, 0, 255);
		uint32_t blue = clamp<uint32_t>(fg_blue + bg_blue, 0, 255);

		*dest = 0xff000000 | (red << 16) | (green << 8) | blue;

		source += 4;
		dest += pitch;
	} while (--count);
#else
	DWORD *fg2rgb = dc_srcblend;
	DWORD *bg2rgb = dc_destblend;
	do {
		DWORD fg = colormap[*source];
		DWORD bg = *dest;

		fg = fg2rgb[fg];
		bg = bg2rgb[bg];
		fg = (fg+bg) | 0x1f07c1f;
		*dest = RGB32k.All[fg & (fg>>15)];
		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Adds all four spans to the screen starting at sx without clamping.
void rt_add4cols_c (int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4];
	pitch = dc_pitch;
	colormap = dc_colormap;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);

	do {
		for (int i = 0; i < 4; i++)
		{
			uint32_t fg = shade_pal_index(colormap[source[i]], light);
			uint32_t fg_red = (fg >> 16) & 0xff;
			uint32_t fg_green = (fg >> 8) & 0xff;
			uint32_t fg_blue = fg & 0xff;

			uint32_t bg_red = (dest[i] >> 16) & 0xff;
			uint32_t bg_green = (dest[i] >> 8) & 0xff;
			uint32_t bg_blue = (dest[i]) & 0xff;

			uint32_t red = clamp<uint32_t>(fg_red + bg_red, 0, 255);
			uint32_t green = clamp<uint32_t>(fg_green + bg_green, 0, 255);
			uint32_t blue = clamp<uint32_t>(fg_blue + bg_blue, 0, 255);

			dest[i] = 0xff000000 | (red << 16) | (green << 8) | blue;
		}

		source += 4;
		dest += pitch;
	} while (--count);
#else
	DWORD *fg2rgb = dc_srcblend;
	DWORD *bg2rgb = dc_destblend;

	do {
		DWORD fg = colormap[source[0]];
		DWORD bg = dest[0];
		fg = fg2rgb[fg];
		bg = bg2rgb[bg];
		fg = (fg+bg) | 0x1f07c1f;
		dest[0] = RGB32k.All[fg & (fg>>15)];

		fg = colormap[source[1]];
		bg = dest[1];
		fg = fg2rgb[fg];
		bg = bg2rgb[bg];
		fg = (fg+bg) | 0x1f07c1f;
		dest[1] = RGB32k.All[fg & (fg>>15)];


		fg = colormap[source[2]];
		bg = dest[2];
		fg = fg2rgb[fg];
		bg = bg2rgb[bg];
		fg = (fg+bg) | 0x1f07c1f;
		dest[2] = RGB32k.All[fg & (fg>>15)];

		fg = colormap[source[3]];
		bg = dest[3];
		fg = fg2rgb[fg];
		bg = bg2rgb[bg];
		fg = (fg+bg) | 0x1f07c1f;
		dest[3] = RGB32k.All[fg & (fg>>15)];

		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Translates and adds one span at hx to the screen at sx without clamping.
void rt_tlateadd1col (int hx, int sx, int yl, int yh)
{
	rt_Translate1col(dc_translation, hx, yl, yh);
	rt_add1col(hx, sx, yl, yh);
}

// Translates and adds all four spans to the screen starting at sx without clamping.
void rt_tlateadd4cols (int sx, int yl, int yh)
{
	rt_Translate4cols(dc_translation, yl, yh);
	rt_add4cols(sx, yl, yh);
}

// Shades one span at hx to the screen at sx.
void rt_shaded1col (int hx, int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	colormap = dc_colormap;
	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch;

#ifndef PALETTEOUTPUT
	uint32_t fg = shade_pal_index(dc_color, calc_light_multiplier(0));
	uint32_t fg_red = (fg >> 16) & 0xff;
	uint32_t fg_green = (fg >> 8) & 0xff;
	uint32_t fg_blue = fg & 0xff;

	do {
		uint32_t alpha = colormap[*source];
		uint32_t inv_alpha = 64 - alpha;

		uint32_t bg_red = (*dest >> 16) & 0xff;
		uint32_t bg_green = (*dest >> 8) & 0xff;
		uint32_t bg_blue = (*dest) & 0xff;

		uint32_t red = (fg_red * alpha + bg_red * inv_alpha) / 64;
		uint32_t green = (fg_green * alpha + bg_green * inv_alpha) / 64;
		uint32_t blue = (fg_blue * alpha + bg_blue * inv_alpha) / 64;

		*dest = 0xff000000 | (red << 16) | (green << 8) | blue;
		source += 4;
		dest += pitch;
	} while (--count);
#else
	DWORD *fgstart;
	fgstart = &Col2RGB8[0][dc_color];

	do {
		DWORD val = colormap[*source];
		DWORD fg = fgstart[val<<8];
		val = (Col2RGB8[64-val][*dest] + fg) | 0x1f07c1f;
		*dest = RGB32k.All[val & (val>>15)];
		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Shades all four spans to the screen starting at sx.
void rt_shaded4cols_c (int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	colormap = dc_colormap;
	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4];
	pitch = dc_pitch;

#ifndef PALETTEOUTPUT
	uint32_t fg = shade_pal_index(dc_color, calc_light_multiplier(0));
	uint32_t fg_red = (fg >> 16) & 0xff;
	uint32_t fg_green = (fg >> 8) & 0xff;
	uint32_t fg_blue = fg & 0xff;

	do {
		for (int i = 0; i < 4; i++)
		{
			uint32_t alpha = colormap[source[i]];
			uint32_t inv_alpha = 64 - alpha;

			uint32_t bg_red = (dest[i] >> 16) & 0xff;
			uint32_t bg_green = (dest[i] >> 8) & 0xff;
			uint32_t bg_blue = (dest[i]) & 0xff;

			uint32_t red = (fg_red * alpha + bg_red * inv_alpha) / 64;
			uint32_t green = (fg_green * alpha + bg_green * inv_alpha) / 64;
			uint32_t blue = (fg_blue * alpha + bg_blue * inv_alpha) / 64;

			dest[i] = 0xff000000 | (red << 16) | (green << 8) | blue;
		}
		source += 4;
		dest += pitch;
	} while (--count);
#else
	DWORD *fgstart;
	fgstart = &Col2RGB8[0][dc_color];

	do {
		DWORD val;
		
		val = colormap[source[0]];
		val = (Col2RGB8[64-val][dest[0]] + fgstart[val<<8]) | 0x1f07c1f;
		dest[0] = RGB32k.All[val & (val>>15)];

		val = colormap[source[1]];
		val = (Col2RGB8[64-val][dest[1]] + fgstart[val<<8]) | 0x1f07c1f;
		dest[1] = RGB32k.All[val & (val>>15)];

		val = colormap[source[2]];
		val = (Col2RGB8[64-val][dest[2]] + fgstart[val<<8]) | 0x1f07c1f;
		dest[2] = RGB32k.All[val & (val>>15)];

		val = colormap[source[3]];
		val = (Col2RGB8[64-val][dest[3]] + fgstart[val<<8]) | 0x1f07c1f;
		dest[3] = RGB32k.All[val & (val>>15)];

		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Adds one span at hx to the screen at sx with clamping.
void rt_addclamp1col (int hx, int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch;
	colormap = dc_colormap;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);

	do {
		uint32_t fg = shade_pal_index(colormap[*source], light);
		uint32_t fg_red = (fg >> 16) & 0xff;
		uint32_t fg_green = (fg >> 8) & 0xff;
		uint32_t fg_blue = fg & 0xff;

		uint32_t bg_red = (*dest >> 16) & 0xff;
		uint32_t bg_green = (*dest >> 8) & 0xff;
		uint32_t bg_blue = (*dest) & 0xff;

		uint32_t red = clamp<uint32_t>(fg_red + bg_red, 0, 255);
		uint32_t green = clamp<uint32_t>(fg_green + bg_green, 0, 255);
		uint32_t blue = clamp<uint32_t>(fg_blue + bg_blue, 0, 255);

		*dest = 0xff000000 | (red << 16) | (green << 8) | blue;
		source += 4;
		dest += pitch;
	} while (--count);
#else
	DWORD *fg2rgb = dc_srcblend;
	DWORD *bg2rgb = dc_destblend;

	do {
		DWORD a = fg2rgb[colormap[*source]] + bg2rgb[*dest];
		DWORD b = a;

		a |= 0x01f07c1f;
		b &= 0x40100400;
		a &= 0x3fffffff;
		b = b - (b >> 5);
		a |= b;
		*dest = RGB32k.All[(a>>15) & a];
		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Adds all four spans to the screen starting at sx with clamping.
void rt_addclamp4cols_c (int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4];
	pitch = dc_pitch;
	colormap = dc_colormap;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);

	do {
		for (int i = 0; i < 4; i++)
		{
			uint32_t fg = shade_pal_index(colormap[source[i]], light);
			uint32_t fg_red = (fg >> 16) & 0xff;
			uint32_t fg_green = (fg >> 8) & 0xff;
			uint32_t fg_blue = fg & 0xff;

			uint32_t bg_red = (dest[i] >> 16) & 0xff;
			uint32_t bg_green = (dest[i] >> 8) & 0xff;
			uint32_t bg_blue = (dest[i]) & 0xff;

			uint32_t red = clamp<uint32_t>(fg_red + bg_red, 0, 255);
			uint32_t green = clamp<uint32_t>(fg_green + bg_green, 0, 255);
			uint32_t blue = clamp<uint32_t>(fg_blue + bg_blue, 0, 255);

			dest[i] = 0xff000000 | (red << 16) | (green << 8) | blue;
		}
		source += 4;
		dest += pitch;
	} while (--count);
#else
	DWORD *fg2rgb = dc_srcblend;
	DWORD *bg2rgb = dc_destblend;

	do {
		DWORD a = fg2rgb[colormap[source[0]]] + bg2rgb[dest[0]];
		DWORD b = a;

		a |= 0x01f07c1f;
		b &= 0x40100400;
		a &= 0x3fffffff;
		b = b - (b >> 5);
		a |= b;
		dest[0] = RGB32k.All[(a>>15) & a];

		a = fg2rgb[colormap[source[1]]] + bg2rgb[dest[1]];
		b = a;
		a |= 0x01f07c1f;
		b &= 0x40100400;
		a &= 0x3fffffff;
		b = b - (b >> 5);
		a |= b;
		dest[1] = RGB32k.All[(a>>15) & a];

		a = fg2rgb[colormap[source[2]]] + bg2rgb[dest[2]];
		b = a;
		a |= 0x01f07c1f;
		b &= 0x40100400;
		a &= 0x3fffffff;
		b = b - (b >> 5);
		a |= b;
		dest[2] = RGB32k.All[(a>>15) & a];

		a = fg2rgb[colormap[source[3]]] + bg2rgb[dest[3]];
		b = a;
		a |= 0x01f07c1f;
		b &= 0x40100400;
		a &= 0x3fffffff;
		b = b - (b >> 5);
		a |= b;
		dest[3] = RGB32k.All[(a>>15) & a];

		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Translates and adds one span at hx to the screen at sx with clamping.
void rt_tlateaddclamp1col (int hx, int sx, int yl, int yh)
{
	rt_Translate1col(dc_translation, hx, yl, yh);
	rt_addclamp1col(hx, sx, yl, yh);
}

// Translates and adds all four spans to the screen starting at sx with clamping.
void rt_tlateaddclamp4cols (int sx, int yl, int yh)
{
	rt_Translate4cols(dc_translation, yl, yh);
	rt_addclamp4cols(sx, yl, yh);
}

// Subtracts one span at hx to the screen at sx with clamping.
void rt_subclamp1col (int hx, int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch;
	colormap = dc_colormap;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);

	do {
		uint32_t fg = shade_pal_index(colormap[*source], light);
		uint32_t fg_red = (fg >> 16) & 0xff;
		uint32_t fg_green = (fg >> 8) & 0xff;
		uint32_t fg_blue = fg & 0xff;

		uint32_t bg_red = (*dest >> 16) & 0xff;
		uint32_t bg_green = (*dest >> 8) & 0xff;
		uint32_t bg_blue = (*dest) & 0xff;

		uint32_t red = clamp<uint32_t>(256 - fg_red + bg_red, 256, 256 + 255) - 256;
		uint32_t green = clamp<uint32_t>(256 - fg_green + bg_green, 256, 256 + 255) - 256;
		uint32_t blue = clamp<uint32_t>(256 - fg_blue + bg_blue, 256, 256 + 255) - 256;

		*dest = 0xff000000 | (red << 16) | (green << 8) | blue;
		source += 4;
		dest += pitch;
	} while (--count);
#else
	DWORD *fg2rgb = dc_srcblend;
	DWORD *bg2rgb = dc_destblend;
	do {
		DWORD a = (fg2rgb[colormap[*source]] | 0x40100400) - bg2rgb[*dest];
		DWORD b = a;

		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		*dest = RGB32k.All[(a>>15) & a];
		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Subtracts all four spans to the screen starting at sx with clamping.
void rt_subclamp4cols (int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4];
	pitch = dc_pitch;
	colormap = dc_colormap;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);

	do {
		for (int i = 0; i < 4; i++)
		{
			uint32_t fg = shade_pal_index(colormap[source[i]], light);
			uint32_t fg_red = (fg >> 16) & 0xff;
			uint32_t fg_green = (fg >> 8) & 0xff;
			uint32_t fg_blue = fg & 0xff;

			uint32_t bg_red = (dest[i] >> 16) & 0xff;
			uint32_t bg_green = (dest[i] >> 8) & 0xff;
			uint32_t bg_blue = (dest[i]) & 0xff;

			uint32_t red = clamp<uint32_t>(256 - fg_red + bg_red, 256, 256 + 255) - 256;
			uint32_t green = clamp<uint32_t>(256 - fg_green + bg_green, 256, 256 + 255) - 256;
			uint32_t blue = clamp<uint32_t>(256 - fg_blue + bg_blue, 256, 256 + 255) - 256;

			dest[i] = 0xff000000 | (red << 16) | (green << 8) | blue;
		}

		source += 4;
		dest += pitch;
	} while (--count);
#else
	DWORD *fg2rgb = dc_srcblend;
	DWORD *bg2rgb = dc_destblend;
	do {
		DWORD a = (fg2rgb[colormap[source[0]]] | 0x40100400) - bg2rgb[dest[0]];
		DWORD b = a;

		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		dest[0] = RGB32k.All[(a>>15) & a];

		a = (fg2rgb[colormap[source[1]]] | 0x40100400) - bg2rgb[dest[1]];
		b = a;
		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		dest[1] = RGB32k.All[(a>>15) & a];

		a = (fg2rgb[colormap[source[2]]] | 0x40100400) - bg2rgb[dest[2]];
		b = a;
		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		dest[2] = RGB32k.All[(a>>15) & a];

		a = (fg2rgb[colormap[source[3]]] | 0x40100400) - bg2rgb[dest[3]];
		b = a;
		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		dest[3] = RGB32k.All[(a>>15) & a];

		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Translates and subtracts one span at hx to the screen at sx with clamping.
void rt_tlatesubclamp1col (int hx, int sx, int yl, int yh)
{
	rt_Translate1col(dc_translation, hx, yl, yh);
	rt_subclamp1col(hx, sx, yl, yh);
}

// Translates and subtracts all four spans to the screen starting at sx with clamping.
void rt_tlatesubclamp4cols (int sx, int yl, int yh)
{
	rt_Translate4cols(dc_translation, yl, yh);
	rt_subclamp4cols(sx, yl, yh);
}

// Subtracts one span at hx from the screen at sx with clamping.
void rt_revsubclamp1col (int hx, int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	DWORD *fg2rgb = dc_srcblend;
	DWORD *bg2rgb = dc_destblend;
	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch;
	colormap = dc_colormap;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);

	do {
		uint32_t fg = shade_pal_index(colormap[*source], light);
		uint32_t fg_red = (fg >> 16) & 0xff;
		uint32_t fg_green = (fg >> 8) & 0xff;
		uint32_t fg_blue = fg & 0xff;

		uint32_t bg_red = (*dest >> 16) & 0xff;
		uint32_t bg_green = (*dest >> 8) & 0xff;
		uint32_t bg_blue = (*dest) & 0xff;

		uint32_t red = clamp<uint32_t>(256 + fg_red - bg_red, 256, 256 + 255) - 256;
		uint32_t green = clamp<uint32_t>(256 + fg_green - bg_green, 256, 256 + 255) - 256;
		uint32_t blue = clamp<uint32_t>(256 + fg_blue - bg_blue, 256, 256 + 255) - 256;

		*dest = 0xff000000 | (red << 16) | (green << 8) | blue;
		source += 4;
		dest += pitch;
	} while (--count);
#else
	do {
		DWORD a = (bg2rgb[*dest] | 0x40100400) - fg2rgb[colormap[*source]];
		DWORD b = a;

		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		*dest = RGB32k.All[(a>>15) & a];
		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Subtracts all four spans from the screen starting at sx with clamping.
void rt_revsubclamp4cols (int sx, int yl, int yh)
{
	BYTE *colormap;
	canvas_pixel_t *source;
	canvas_pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	DWORD *fg2rgb = dc_srcblend;
	DWORD *bg2rgb = dc_destblend;
	dest = ylookup[yl] + sx + dc_destorg;
	source = &dc_temp[yl*4];
	pitch = dc_pitch;
	colormap = dc_colormap;

#ifndef PALETTEOUTPUT
	uint32_t light = calc_light_multiplier(dc_light);

	do {
		for (int i = 0; i < 4; i++)
		{
			uint32_t fg = shade_pal_index(colormap[source[i]], light);
			uint32_t fg_red = (fg >> 16) & 0xff;
			uint32_t fg_green = (fg >> 8) & 0xff;
			uint32_t fg_blue = fg & 0xff;

			uint32_t bg_red = (dest[i] >> 16) & 0xff;
			uint32_t bg_green = (dest[i] >> 8) & 0xff;
			uint32_t bg_blue = (dest[i]) & 0xff;

			uint32_t red = clamp<uint32_t>(256 + fg_red - bg_red, 256, 256 + 255) - 256;
			uint32_t green = clamp<uint32_t>(256 + fg_green - bg_green, 256, 256 + 255) - 256;
			uint32_t blue = clamp<uint32_t>(256 + fg_blue - bg_blue, 256, 256 + 255) - 256;

			dest[i] = 0xff000000 | (red << 16) | (green << 8) | blue;
		}

		source += 4;
		dest += pitch;
	} while (--count);
#else
	do {
		DWORD a = (bg2rgb[dest[0]] | 0x40100400) - fg2rgb[colormap[source[0]]];
		DWORD b = a;

		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		dest[0] = RGB32k.All[(a>>15) & a];

		a = (bg2rgb[dest[1]] | 0x40100400) - fg2rgb[colormap[source[1]]];
		b = a;
		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		dest[1] = RGB32k.All[(a>>15) & a];

		a = (bg2rgb[dest[2]] | 0x40100400) - fg2rgb[colormap[source[2]]];
		b = a;
		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		dest[2] = RGB32k.All[(a>>15) & a];

		a = (bg2rgb[dest[3]] | 0x40100400) - fg2rgb[colormap[source[3]]];
		b = a;
		b &= 0x40100400;
		b = b - (b >> 5);
		a &= b;
		a |= 0x01f07c1f;
		dest[3] = RGB32k.All[(a>>15) & a];

		source += 4;
		dest += pitch;
	} while (--count);
#endif
}

// Translates and subtracts one span at hx from the screen at sx with clamping.
void rt_tlaterevsubclamp1col (int hx, int sx, int yl, int yh)
{
	rt_Translate1col(dc_translation, hx, yl, yh);
	rt_revsubclamp1col(hx, sx, yl, yh);
}

// Translates and subtracts all four spans from the screen starting at sx with clamping.
void rt_tlaterevsubclamp4cols (int sx, int yl, int yh)
{
	rt_Translate4cols(dc_translation, yl, yh);
	rt_revsubclamp4cols(sx, yl, yh);
}

// Copies all spans in all four columns to the screen starting at sx.
// sx should be dword-aligned.
void rt_draw4cols (int sx)
{
	int x, bad;
	unsigned int maxtop, minbot, minnexttop;

	// Place a dummy "span" in each column. These don't get
	// drawn. They're just here to avoid special cases in the
	// max/min calculations below.
	for (x = 0; x < 4; ++x)
	{
		dc_ctspan[x][0] = screen->GetHeight()+1;
		dc_ctspan[x][1] = screen->GetHeight();
	}

#ifdef X86_ASM
	// Setup assembly routines for changed colormaps or other parameters.
	if (hcolfunc_post4 == rt_shaded4cols)
	{
		R_SetupShadedCol();
	}
	else if (hcolfunc_post4 == rt_addclamp4cols || hcolfunc_post4 == rt_tlateaddclamp4cols)
	{
		R_SetupAddClampCol();
	}
	else if (hcolfunc_post4 == rt_add4cols || hcolfunc_post4 == rt_tlateadd4cols)
	{
		R_SetupAddCol();
	}
#endif

	for (;;)
	{
		// If a column is out of spans, mark it as such
		bad = 0;
		minnexttop = 0xffffffff;
		for (x = 0; x < 4; ++x)
		{
			if (horizspan[x] >= dc_ctspan[x])
			{
				bad |= 1 << x;
			}
			else if ((horizspan[x]+2)[0] < minnexttop)
			{
				minnexttop = (horizspan[x]+2)[0];
			}
		}
		// Once all columns are out of spans, we're done
		if (bad == 15)
		{
			return;
		}

		// Find the largest shared area for the spans in each column
		maxtop = MAX (MAX (horizspan[0][0], horizspan[1][0]),
					  MAX (horizspan[2][0], horizspan[3][0]));
		minbot = MIN (MIN (horizspan[0][1], horizspan[1][1]),
					  MIN (horizspan[2][1], horizspan[3][1]));

		// If there is no shared area with these spans, draw each span
		// individually and advance to the next spans until we reach a shared area.
		// However, only draw spans down to the highest span in the next set of
		// spans. If we allow the entire height of a span to be drawn, it could
		// prevent any more shared areas from being drawn in these four columns.
		//
		// Example: Suppose we have the following arrangement:
		//			A CD
		//			A CD
		//			 B D
		//			 B D
		//			aB D
		//			aBcD
		//			aBcD
		//			aBc
		//
		// If we draw the entire height of the spans, we end up drawing this first:
		//			A CD
		//			A CD
		//			 B D
		//			 B D
		//			 B D
		//			 B D
		//			 B D
		//			 B D
		//			 B
		//
		// This leaves only the "a" and "c" columns to be drawn, and they are not
		// part of a shared area, but if we can include B and D with them, we can
		// get a shared area. So we cut off everything in the first set just
		// above the "a" column and end up drawing this first:
		//			A CD
		//			A CD
		//			 B D
		//			 B D
		//
		// Then the next time through, we have the following arrangement with an
		// easily shared area to draw:
		//			aB D
		//			aBcD
		//			aBcD
		//			aBc
		if (bad != 0 || maxtop > minbot)
		{
			int drawcount = 0;
			for (x = 0; x < 4; ++x)
			{
				if (!(bad & 1))
				{
					if (horizspan[x][1] < minnexttop)
					{
						hcolfunc_post1 (x, sx+x, horizspan[x][0], horizspan[x][1]);
						horizspan[x] += 2;
						drawcount++;
					}
					else if (minnexttop > horizspan[x][0])
					{
						hcolfunc_post1 (x, sx+x, horizspan[x][0], minnexttop-1);
						horizspan[x][0] = minnexttop;
						drawcount++;
					}
				}
				bad >>= 1;
			}
			// Drawcount *should* always be non-zero. The reality is that some situations
			// can make this not true. Unfortunately, I'm not sure what those situations are.
			if (drawcount == 0)
			{
				return;
			}
			continue;
		}

		// Draw any span fragments above the shared area.
		for (x = 0; x < 4; ++x)
		{
			if (maxtop > horizspan[x][0])
			{
				hcolfunc_post1 (x, sx+x, horizspan[x][0], maxtop-1);
			}
		}

		// Draw the shared area.
		hcolfunc_post4 (sx, maxtop, minbot);

		// For each column, if part of the span is past the shared area,
		// set its top to just below the shared area. Otherwise, advance
		// to the next span in that column.
		for (x = 0; x < 4; ++x)
		{
			if (minbot < horizspan[x][1])
			{
				horizspan[x][0] = minbot+1;
			}
			else
			{
				horizspan[x] += 2;
			}
		}
	}
}

// Before each pass through a rendering loop that uses these routines,
// call this function to set up the span pointers.
void rt_initcols (canvas_pixel_t *buff)
{
	int y;

	dc_temp = buff == NULL ? dc_tempbuff : buff;
	for (y = 3; y >= 0; y--)
		horizspan[y] = dc_ctspan[y] = &dc_tspans[y][0];
}

// Stretches a column into a temporary buffer which is later
// drawn to the screen along with up to three other columns.
void R_DrawColumnHorizP_C (void)
{
	int count = dc_count;
	canvas_pixel_t *dest;
	fixed_t fracstep;
	fixed_t frac;

	if (count <= 0)
		return;

	{
		int x = dc_x & 3;
		unsigned int **span;
		
		span = &dc_ctspan[x];
		(*span)[0] = dc_yl;
		(*span)[1] = dc_yh;
		*span += 2;
		dest = &dc_temp[x + 4*dc_yl];
	}
	fracstep = dc_iscale;
	frac = dc_texturefrac;

	{
		const BYTE *source = dc_source;

		if (count & 1) {
			*dest = source[frac>>FRACBITS]; dest += 4; frac += fracstep;
		}
		if (count & 2) {
			dest[0] = source[frac>>FRACBITS]; frac += fracstep;
			dest[4] = source[frac>>FRACBITS]; frac += fracstep;
			dest += 8;
		}
		if (count & 4) {
			dest[0] = source[frac>>FRACBITS]; frac += fracstep;
			dest[4] = source[frac>>FRACBITS]; frac += fracstep;
			dest[8] = source[frac>>FRACBITS]; frac += fracstep;
			dest[12]= source[frac>>FRACBITS]; frac += fracstep;
			dest += 16;
		}
		count >>= 3;
		if (!count) return;

		do
		{
			dest[0] = source[frac>>FRACBITS]; frac += fracstep;
			dest[4] = source[frac>>FRACBITS]; frac += fracstep;
			dest[8] = source[frac>>FRACBITS]; frac += fracstep;
			dest[12]= source[frac>>FRACBITS]; frac += fracstep;
			dest[16]= source[frac>>FRACBITS]; frac += fracstep;
			dest[20]= source[frac>>FRACBITS]; frac += fracstep;
			dest[24]= source[frac>>FRACBITS]; frac += fracstep;
			dest[28]= source[frac>>FRACBITS]; frac += fracstep;
			dest += 32;
		} while (--count);
	}
}

// [RH] Just fills a column with a given color
void R_FillColumnHorizP (void)
{
	int count = dc_count;
	BYTE color = dc_color;
	canvas_pixel_t *dest;

	if (count <= 0)
		return;

	{
		int x = dc_x & 3;
		unsigned int **span = &dc_ctspan[x];

		(*span)[0] = dc_yl;
		(*span)[1] = dc_yh;
		*span += 2;
		dest = &dc_temp[x + 4*dc_yl];
	}

	if (count & 1) {
		*dest = color;
		dest += 4;
	}
	if (!(count >>= 1))
		return;
	do {
		dest[0] = color; dest[4] = color;
		dest += 8;
	} while (--count);
}

// Same as R_DrawMaskedColumn() except that it always uses R_DrawColumnHoriz().

void R_DrawMaskedColumnHoriz (const BYTE *column, const FTexture::Span *span)
{
	const fixed_t texturemid = FLOAT2FIXED(dc_texturemid);
	while (span->Length != 0)
	{
		const int length = span->Length;
		const int top = span->TopOffset;

		// calculate unclipped screen coordinates for post
		dc_yl = xs_RoundToInt(sprtopscreen + spryscale * top);
		dc_yh = xs_RoundToInt(sprtopscreen + spryscale * (top + length) - 1);

		if (sprflipvert)
		{
			swapvalues (dc_yl, dc_yh);
		}

		if (dc_yh >= mfloorclip[dc_x])
		{
			dc_yh = mfloorclip[dc_x] - 1;
		}
		if (dc_yl < mceilingclip[dc_x])
		{
			dc_yl = mceilingclip[dc_x];
		}

		if (dc_yl <= dc_yh)
		{
			if (sprflipvert)
			{
				dc_texturefrac = (dc_yl*dc_iscale) - (top << FRACBITS)
					- fixed_t(CenterY * dc_iscale) - texturemid;
				const fixed_t maxfrac = length << FRACBITS;
				while (dc_texturefrac >= maxfrac)
				{
					if (++dc_yl > dc_yh)
						goto nextpost;
					dc_texturefrac += dc_iscale;
				}
				fixed_t endfrac = dc_texturefrac + (dc_yh-dc_yl)*dc_iscale;
				while (endfrac < 0)
				{
					if (--dc_yh < dc_yl)
						goto nextpost;
					endfrac -= dc_iscale;
				}
			}
			else
			{
				dc_texturefrac = texturemid - (top << FRACBITS)
					+ (dc_yl*dc_iscale) - fixed_t((CenterY-1) * dc_iscale);
				while (dc_texturefrac < 0)
				{
					if (++dc_yl > dc_yh)
						goto nextpost;
					dc_texturefrac += dc_iscale;
				}
				fixed_t endfrac = dc_texturefrac + (dc_yh-dc_yl)*dc_iscale;
				const fixed_t maxfrac = length << FRACBITS;
				if (dc_yh < mfloorclip[dc_x]-1 && endfrac < maxfrac - dc_iscale)
				{
					dc_yh++;
				}
				else while (endfrac >= maxfrac)
				{
					if (--dc_yh < dc_yl)
						goto nextpost;
					endfrac -= dc_iscale;
				}
			}
			dc_source = column + top;
			dc_dest = ylookup[dc_yl] + dc_x + dc_destorg;
			dc_count = dc_yh - dc_yl + 1;
			hcolfunc_pre ();
		}
nextpost:
		span++;
	}

	if (sprflipvert)
	{
		unsigned int *front = horizspan[dc_x&3];
		unsigned int *back = dc_ctspan[dc_x&3] - 2;

		// Reorder the posts so that they get drawn top-to-bottom
		// instead of bottom-to-top.
		while (front < back)
		{
			swapvalues (front[0], back[0]);
			swapvalues (front[1], back[1]);
			front += 2;
			back -= 2;
		}
	}
}

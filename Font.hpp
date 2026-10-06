#pragma once

#include "GL.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

//Font shapes UTF-8 text with HarfBuzz and rasterizes glyphs with FreeType, packing every
//glyph it has ever drawn into one shared GPU texture atlas. Drawing the same character
//again -- even later, even in a different line -- costs nothing extra: rasterization and
//atlas space are paid for once per *unique glyph*, not once per line or per frame. That's
//what lets this scale to a game with lots of text without pre-rendering every line to its
//own texture.
//
//References:
// freetype tutorial: https://freetype.org/freetype2/docs/tutorial/step1.html
// harfbuzz shaping example: https://harfbuzz.github.io/a-simple-shaping-example.html
struct Font {
	//pixel_height: full glyph height, in pixels, that this Font will shape + rasterize at.
	//atlas_size: the atlas texture is atlas_size x atlas_size pixels; glyphs are packed in with
	// a simple shelf packer and it is never grown, so pick something generous for how much
	// distinct-glyph variety you expect (a few hundred glyphs at typical UI sizes fit easily in 1024x1024).
	Font(std::string const &font_path, unsigned int pixel_height, uint32_t atlas_size = 1024);
	~Font();

	Font(Font const &) = delete; //owns a GL texture + FreeType/HarfBuzz handles; not meant to be copied

	//One shaped glyph, ready to become a textured quad:
	struct PositionedGlyph {
		glm::vec2 min, max; //quad corners, in pixels, relative to the start of the line (+x right, +y DOWN -- image convention)
		glm::vec2 uv_min, uv_max; //matching texture coordinates into 'texture'
	};

	//Shapes 'utf8_text' and returns one quad per visible glyph (space and other zero-area
	//glyphs are omitted, though they still affect layout). Rasterizes + atlases any glyph
	//that hasn't been seen by this Font before.
	//If out_advance is non-null, it receives how far the pen moved (pixels), so callers can
	//place multiple runs on one line.
	//NOTE: logically const (what text you get back never changes), even though it lazily
	//fills the atlas/glyph cache the first time each glyph is seen -- see 'mutable' below.
	std::vector< PositionedGlyph > layout(std::string const &utf8_text, float *out_advance = nullptr) const;

	GLuint texture = 0; //atlas texture: single-channel coverage, swizzled to sample as (1,1,1,coverage) -- see .cpp
	unsigned int pixel_height;
	unsigned int line_height; //recommended distance (pixels) between successive baselines, from the font's own metrics

private:
	struct AtlasGlyph {
		glm::ivec2 size = glm::ivec2(0); //rasterized bitmap size, pixels
		glm::ivec2 bearing = glm::ivec2(0); //offset from the pen to the bitmap's top-left corner, pixels (+x right, +y UP -- freetype convention)
		glm::vec2 uv_min = glm::vec2(0.0f), uv_max = glm::vec2(0.0f);
	};
	//Rasterizes + packs a glyph into the atlas the first time it's requested; returns the cached entry after that:
	AtlasGlyph const &get_glyph(hb_codepoint_t glyph_index) const;

	FT_Library library = nullptr;
	FT_Face face = nullptr;
	hb_font_t *hb_font = nullptr;

	//cache + packing state: mutated lazily from the const layout()/get_glyph(), so mutable:
	mutable std::unordered_map< hb_codepoint_t, AtlasGlyph > glyphs;
	uint32_t atlas_size;
	mutable glm::uvec2 pack_at = glm::uvec2(0, 0);
	mutable uint32_t shelf_height = 0;
};

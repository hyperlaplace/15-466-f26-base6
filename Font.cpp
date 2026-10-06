#include "Font.hpp"

#include "gl_errors.hpp"

#include <hb-ft.h>
#include FT_MULTIPLE_MASTERS_H

#include <algorithm>
#include <stdexcept>

Font::Font(std::string const &font_path, unsigned int pixel_height_, uint32_t atlas_size_)
	: pixel_height(pixel_height_), atlas_size(atlas_size_) {

	if (FT_Init_FreeType(&library)) {
		throw std::runtime_error("FT_Init_FreeType failed.");
	}
	if (FT_New_Face(library, font_path.c_str(), 0, &face)) {
		throw std::runtime_error("Font: FT_New_Face failed to load '" + font_path + "'.");
	}
	//variable fonts (e.g. Roboto[wdth,wght].ttf) load at their default (regular) weight: ask for bold.
	// (static fonts have no variation axes, so FT_Get_MM_Var fails and this does nothing)
	FT_MM_Var *variations = nullptr;
	if (FT_Get_MM_Var(face, &variations) == 0) {
		std::vector< FT_Fixed > coords(variations->num_axis);
		for (FT_UInt i = 0; i < variations->num_axis; ++i) {
			FT_Var_Axis const &axis = variations->axis[i];
			coords[i] = axis.def;
			if (axis.tag == FT_MAKE_TAG('w','g','h','t')) {
				coords[i] = std::min(axis.maximum, std::max(axis.minimum, FT_Fixed(700) << 16));
			}
		}
		FT_Set_Var_Design_Coordinates(face, variations->num_axis, coords.data());
		FT_Done_MM_Var(library, variations);
	}
	FT_Set_Pixel_Sizes(face, 0, pixel_height); //must happen before creating the hb_font below
	line_height = unsigned(face->size->metrics.height >> 6);

	hb_font = hb_ft_font_create_referenced(face);

	//allocate the atlas texture, cleared to zero (== fully-transparent everywhere):
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	std::vector< uint8_t > blank(size_t(atlas_size) * atlas_size, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlas_size, atlas_size, 0, GL_RED, GL_UNSIGNED_BYTE, blank.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	//This atlas only has one channel (coverage). The shaders this project already has (e.g.
	//ColorTextureProgram) expect a texture with color in RGB and opacity in A, so swizzle the
	//single channel to look like that: RGB reads back as solid white, A reads back as our data.
	//That way text can be drawn with the *existing* textured-quad shader instead of a new one.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
	glBindTexture(GL_TEXTURE_2D, 0);

	GL_ERRORS();
}

Font::~Font() {
	glDeleteTextures(1, &texture);
	if (hb_font) hb_font_destroy(hb_font);
	if (face) FT_Done_Face(face);
	if (library) FT_Done_FreeType(library);
}

Font::AtlasGlyph const &Font::get_glyph(hb_codepoint_t glyph_index) const {
	auto found = glyphs.find(glyph_index);
	if (found != glyphs.end()) return found->second;

	if (FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT)) {
		throw std::runtime_error("Font: FT_Load_Glyph failed for glyph " + std::to_string(glyph_index) + ".");
	}
	if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
		throw std::runtime_error("Font: FT_Render_Glyph failed for glyph " + std::to_string(glyph_index) + ".");
	}

	FT_Bitmap const &bitmap = face->glyph->bitmap;

	AtlasGlyph glyph;
	glyph.size = glm::ivec2(int(bitmap.width), int(bitmap.rows));
	glyph.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top);

	if (bitmap.width > 0 && bitmap.rows > 0) {
		//advance to a new shelf if this glyph doesn't fit at the end of the current row:
		if (pack_at.x + bitmap.width > atlas_size) {
			pack_at.x = 0;
			pack_at.y += shelf_height;
			shelf_height = 0;
		}
		if (pack_at.y + bitmap.rows > atlas_size) {
			throw std::runtime_error(
				"Font glyph atlas ran out of room packing glyph " + std::to_string(glyph_index) +
				" (raise atlas_size, or this Font is being asked to draw more distinct glyphs than it was sized for)."
			);
		}

		//upload straight from FreeType's bitmap -- GL_UNPACK_ROW_LENGTH tells the driver about
		//bitmap.pitch (which can be wider than bitmap.width due to padding), so no manual
		//row-by-row copy is needed here (contrast with freetype-test.cpp, which copies by hand
		//because it isn't uploading to a GL texture):
		glBindTexture(GL_TEXTURE_2D, texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, bitmap.pitch);
		glTexSubImage2D(GL_TEXTURE_2D, 0, pack_at.x, pack_at.y, bitmap.width, bitmap.rows, GL_RED, GL_UNSIGNED_BYTE, bitmap.buffer);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D, 0);

		glyph.uv_min = glm::vec2(pack_at) / float(atlas_size);
		glyph.uv_max = glm::vec2(pack_at + glm::uvec2(bitmap.width, bitmap.rows)) / float(atlas_size);

		pack_at.x += bitmap.width;
		shelf_height = std::max(shelf_height, uint32_t(bitmap.rows));
	}

	GL_ERRORS();

	auto inserted = glyphs.emplace(glyph_index, glyph);
	return inserted.first->second;
}

std::vector< Font::PositionedGlyph > Font::layout(std::string const &utf8_text, float *out_advance) const {
	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, utf8_text.c_str(), -1, 0, -1);
	hb_buffer_guess_segment_properties(buf);
	hb_shape(hb_font, buf, nullptr, 0);

	unsigned int glyph_count = 0;
	hb_glyph_info_t const *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
	hb_glyph_position_t const *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

	std::vector< PositionedGlyph > out;
	out.reserve(glyph_count);

	glm::vec2 pen(0.0f, 0.0f);

	for (unsigned int i = 0; i < glyph_count; ++i) {
		AtlasGlyph const &glyph = get_glyph(glyph_info[i].codepoint);

		if (glyph.size.x > 0 && glyph.size.y > 0) {
			//same y-up (harfbuzz/freetype) -> y-down (image space) flip as in freetype-test.cpp:
			glm::vec2 origin = pen
				+ glm::vec2(glyph_pos[i].x_offset >> 6, -(glyph_pos[i].y_offset >> 6))
				+ glm::vec2(glyph.bearing.x, -glyph.bearing.y);

			PositionedGlyph pg;
			pg.min = origin;
			pg.max = origin + glm::vec2(glyph.size);
			pg.uv_min = glyph.uv_min;
			pg.uv_max = glyph.uv_max;
			out.emplace_back(pg);
		}

		pen += glm::vec2(glyph_pos[i].x_advance >> 6, glyph_pos[i].y_advance >> 6);
	}

	hb_buffer_destroy(buf);

	if (out_advance) *out_advance = pen.x;

	return out;
}

#include "TextRenderer.hpp"

#include "ColorTextureProgram.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/type_ptr.hpp>

//All TextRenderer instances share a vertex buffer + vertex array object, set up at load time
//(same pattern as DrawLines.cpp / DrawSprites.cpp):

static GLuint vertex_buffer = 0;
static GLuint vertex_buffer_for_color_texture_program = 0;

static Load< void > setup_buffers(LoadTagDefault, [](){
	glGenBuffers(1, &vertex_buffer);

	glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);
	glBindVertexArray(vertex_buffer_for_color_texture_program);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

	glVertexAttribPointer(
		color_texture_program->Position_vec4,
		3, GL_FLOAT, GL_FALSE,
		sizeof(TextRenderer::Vertex),
		(GLbyte *)0 + offsetof(TextRenderer::Vertex, Position)
	);
	glEnableVertexAttribArray(color_texture_program->Position_vec4);

	glVertexAttribPointer(
		color_texture_program->Color_vec4,
		4, GL_UNSIGNED_BYTE, GL_TRUE,
		sizeof(TextRenderer::Vertex),
		(GLbyte *)0 + offsetof(TextRenderer::Vertex, Color)
	);
	glEnableVertexAttribArray(color_texture_program->Color_vec4);

	glVertexAttribPointer(
		color_texture_program->TexCoord_vec2,
		2, GL_FLOAT, GL_FALSE,
		sizeof(TextRenderer::Vertex),
		(GLbyte *)0 + offsetof(TextRenderer::Vertex, TexCoord)
	);
	glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	GL_ERRORS();
});

TextRenderer::TextRenderer(glm::mat4 const &world_to_clip_) : world_to_clip(world_to_clip_) {
}

void TextRenderer::draw_text(Font const &font, std::string const &text, glm::vec3 const &anchor, glm::vec3 const &x, glm::vec3 const &y, glm::u8vec4 const &color) {
	std::vector< Font::PositionedGlyph > glyphs = font.layout(text);

	std::vector< Vertex > &verts = batches[&font];
	verts.reserve(verts.size() + glyphs.size() * 6);

	auto to_world = [&](glm::vec2 const &px) {
		//Font::layout hands back pixel coordinates with +y DOWN; x/y here are "one pixel right" /
		//"one pixel up" in world space, so the y component flips sign on the way in:
		return anchor + px.x * x - px.y * y;
	};

	for (auto const &g : glyphs) {
		glm::vec3 TL = to_world(glm::vec2(g.min.x, g.min.y));
		glm::vec3 TR = to_world(glm::vec2(g.max.x, g.min.y));
		glm::vec3 BL = to_world(glm::vec2(g.min.x, g.max.y));
		glm::vec3 BR = to_world(glm::vec2(g.max.x, g.max.y));

		glm::vec2 uvTL(g.uv_min.x, g.uv_min.y);
		glm::vec2 uvTR(g.uv_max.x, g.uv_min.y);
		glm::vec2 uvBL(g.uv_min.x, g.uv_max.y);
		glm::vec2 uvBR(g.uv_max.x, g.uv_max.y);

		verts.emplace_back(TL, color, uvTL);
		verts.emplace_back(BL, color, uvBL);
		verts.emplace_back(TR, color, uvTR);

		verts.emplace_back(TR, color, uvTR);
		verts.emplace_back(BL, color, uvBL);
		verts.emplace_back(BR, color, uvBR);
	}
}

TextRenderer::~TextRenderer() {
	if (batches.empty()) return;

	//NOTE: caller is responsible for GL state around this (blending needs to be on -- glyph
	//quads are fully opaque rectangles with the actual letterform stored in the alpha channel):

	glUseProgram(color_texture_program->program);
	glUniformMatrix4fv(color_texture_program->CLIP_FROM_OBJECT_mat4, 1, GL_FALSE, glm::value_ptr(world_to_clip));

	glBindVertexArray(vertex_buffer_for_color_texture_program);
	glActiveTexture(GL_TEXTURE0);

	for (auto const &[font, verts] : batches) {
		if (verts.empty()) continue;

		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(verts[0]), verts.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glBindTexture(GL_TEXTURE_2D, font->texture);
		glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);

	GL_ERRORS();
}

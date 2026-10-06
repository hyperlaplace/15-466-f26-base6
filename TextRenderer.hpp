#pragma once

/*
 * Helper for drawing text laid out by one or more Fonts.
 *
 * Usage mirrors DrawLines: construct with a world_to_clip matrix, call draw_text()
 * some number of times (across any mix of Fonts), and let the destructor push
 * everything to the GPU -- one draw call per distinct Font used.
 */

#include "Font.hpp"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

struct TextRenderer {
	TextRenderer(glm::mat4 const &world_to_clip);
	~TextRenderer();

	//Draws 'text' shaped by 'font', starting at 'anchor' (world space).
	//'x' and 'y' are world-space vectors corresponding to one pixel of the font's
	//rasterized size, pointing right and up respectively (e.g. if 1 world unit should
	//show H pixel-rows of text, pass x = right * (1.0f / H), y = up * (1.0f / H)).
	void draw_text(Font const &font, std::string const &text,
		glm::vec3 const &anchor,
		glm::vec3 const &x,
		glm::vec3 const &y,
		glm::u8vec4 const &color = glm::u8vec4(0xff));

	glm::mat4 world_to_clip;

	struct Vertex {
		Vertex(glm::vec3 const &Position_, glm::u8vec4 const &Color_, glm::vec2 const &TexCoord_) : Position(Position_), Color(Color_), TexCoord(TexCoord_) { }
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	//one batch of vertices per Font, so each can be drawn with its own atlas texture bound:
	std::unordered_map< Font const *, std::vector< Vertex > > batches;
};

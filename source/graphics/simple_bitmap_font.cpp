#include "simple_bitmap_font.h"
#include <SDL_ttf.h>
#include <format>
#include <vector>
#include <tuple>

using namespace isometric;

static void generate_glyph_surfaces(TTF_Font* font, const std::vector<char>& glyphs, bitmap_font_info& font_info);
static void generate_glyph_srcrects(bitmap_font_info& font_info);
static void generate_glyph_textures(SDL_Renderer* renderer, bitmap_font_info& font_info, bool free_glyph_surfaces = true);

simple_bitmap_font::simple_bitmap_font(SDL_Renderer* renderer, TTF_Font* font, unsigned char start_glyph, unsigned char end_glyph)
    : renderer(renderer), sdl_font(font)
{
    size_t num_glyphs = (static_cast<size_t>(end_glyph) - start_glyph) + 1; // end_glyph is inclusive so + 1
    std::vector<char> glyphs(num_glyphs);

    unsigned char current_glyph = start_glyph;
    for (size_t i = 0; i < num_glyphs; current_glyph++, i++) {
        glyphs[i] = static_cast<char>(current_glyph);
    }

    create(glyphs);
}

simple_bitmap_font::simple_bitmap_font(SDL_Renderer* renderer, TTF_Font* font, const char* glyphs, size_t glyphs_size)
    : simple_bitmap_font(renderer, font, std::vector<char>(glyphs, glyphs + glyphs_size))
{

}

simple_bitmap_font::simple_bitmap_font(SDL_Renderer* renderer, TTF_Font* font, const std::vector<char>& glyphs)
    : renderer(renderer), sdl_font(font)
{
    create(glyphs);
}

simple_bitmap_font::~simple_bitmap_font()
{
    destroy();
}

SDL_Color simple_bitmap_font::set_color(const SDL_Color& color)
{
    SDL_Color old_color = current_color;
    current_color = color;
    return old_color;
}

const SDL_Color& simple_bitmap_font::get_color() const
{
    return current_color;
}

void simple_bitmap_font::draw(const SDL_Point& point, const std::string& text) const
{

}

void simple_bitmap_font::measure(const std::string& text) const
{

}

void simple_bitmap_font::create(const std::vector<char>& glyphs)
{
    generate_glyph_surfaces(sdl_font, glyphs, font_info);
    generate_glyph_srcrects(font_info);
    generate_glyph_textures(renderer, font_info, true);
}

void simple_bitmap_font::destroy()
{
    for (auto& texture_info : font_info.textures) {
        auto& texture = std::get<0>(texture_info);
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }

    font_info.textures.clear();
    font_info.glyphs.clear();

    if (destroy_font && this->sdl_font) {
        TTF_CloseFont(this->sdl_font);
        this->sdl_font = nullptr;
    }
}

static void generate_glyph_surfaces(
    TTF_Font* font,
    const std::vector<char>& glyphs,
    bitmap_font_info& font_info
)
{
    if (!font || glyphs.empty()) return;
    font_info.glyphs.clear();

    for (char character : glyphs) {
        SDL_Surface* surface = TTF_RenderGlyph_Blended(font, character, SDL_Color{ 255, 255, 255, 255 });
        if (surface) {
            font_info.glyphs[character] = glyph_info{
                surface,
                SDL_Rect{ 0, 0, surface->w, surface->h },
                0
            };
        }
    }
}

static void generate_glyph_srcrects(
    bitmap_font_info& font_info
)
{
    if (!font_info.textures.empty() || font_info.glyphs.empty()) return;

    constexpr int max_texture_width = 2048, max_texture_height = 2048;
    int texture_width = 0, texture_height = 0;
    size_t texture_index = 0;
    int x = 0, y = 0, row_height = 0;

    for (auto& pair : font_info.glyphs) {
        char character = pair.first;
        glyph_info& glyph = pair.second;

        // The current row's height should be the tallest glyph:
        if (glyph.surface->h > row_height)
            row_height = glyph.surface->h;

        // If the farthest x-extent has been reached, start a new row:
        if (x + glyph.surface->w >= max_texture_width) {
            x = 0;
            y += row_height;
            row_height = glyph.surface->h;
        }

        // If the farthest y-extent has been reached, configure the texture's dimensions and move onto the next 
        // texture:
        if (y + glyph.surface->h >= max_texture_height) {
            font_info.textures.push_back(
                std::make_tuple<SDL_Texture*, SDL_Rect>(
                    nullptr,
                    SDL_Rect{ 0, 0, texture_width, texture_height }
                    )
            );

            texture_index++;
            texture_width = texture_height = x = y = 0;
        }

        // Set the glyph's source rectangle:
        glyph.srcrect = SDL_Rect{ x, y, glyph.surface->w, glyph.surface->h };
        glyph.texture_index = texture_index;

        // Increment the x coord:
        x += glyph.surface->w;

        // Use the greatest x or y to update the texture_width and texture_height:
        if (x > texture_width) texture_width = x; // Not adding the glyph width since it was done above
        if (y + row_height > texture_height) texture_height = y + row_height;
    }

    // The last texture's dimension needs to be setup after the loop:
    if (texture_width > 0 && texture_height > 0) {
        font_info.textures.push_back(
            std::make_tuple<SDL_Texture*, SDL_Rect>(
                nullptr,
                SDL_Rect{ 0, 0, texture_width, texture_height }
                )
        );
    }
}

static void generate_glyph_textures(
    SDL_Renderer* renderer,
    bitmap_font_info& font_info,
    bool free_glyph_surfaces
)
{
    std::vector<SDL_Surface*> atlas_surfaces;
    for (auto& texture_info : font_info.textures) {
        const SDL_Rect& dimensions = std::get<1>(texture_info);

        SDL_Surface* atlas_surface = SDL_CreateRGBSurface(0, dimensions.w, dimensions.h, 32,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff
#else
            0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000
#endif
        );

        atlas_surfaces.push_back(atlas_surface);
    }

    for (auto& pair : font_info.glyphs) {
        char character = pair.first;
        glyph_info& glyph = pair.second;
        const size_t texture_index = glyph.texture_index;
        // This is the dstrect in this context, since the destination 
        // surface will be the source in the future
        SDL_Rect dstrect = glyph.srcrect;
        SDL_Rect srcrect = SDL_Rect{ 0, 0, dstrect.w, dstrect.h };
        SDL_Surface* dst = atlas_surfaces[texture_index];
        if (dst && glyph.surface)
        {
            SDL_BlitSurface(glyph.surface, &srcrect, dst, &dstrect);
            if (free_glyph_surfaces) {
                SDL_FreeSurface(glyph.surface);
                glyph.surface = nullptr;
            }
        }
    }

    for (size_t texture_index = 0; texture_index < font_info.textures.size(); texture_index++) {
        auto& texture = std::get<0>(font_info.textures[texture_index]);
        auto& surface = atlas_surfaces[texture_index];

        if (surface) {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
            surface = nullptr;
        }
    }
}
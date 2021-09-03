#pragma once
#include <isometric.h>
#include <memory>
#include "../graphics/simple_bitmap_font.h"

namespace isometric::game {

    class fps_display_module : public isometric::module
    {
    private:
        //std::unique_ptr<simple_bitmap_font> bitmap_font;
        std::unordered_map<char, SDL_Rect> glyph_source_rects;
        SDL_Texture* glyphs_texture = nullptr;

    protected:
        void on_registered() override;
        void on_unregister() override;

        void on_update(double delta_time) override;
        void on_late_update(double delta_time) override;
        void on_fixed_update(double fixed_delta_time) override;
    };

}
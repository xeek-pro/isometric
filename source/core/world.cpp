#include <SDL.h>
#include "world.h"
#include "input.h"
#include <iostream>

using namespace isometric;

world::world(std::shared_ptr<tile_map> map, std::shared_ptr<camera> main_camera) :
    map(map),
    transform(main_camera, map)
{
    selected_world_tile = {
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max()
    };

    if (main_camera != nullptr)
    {
        cameras.push_back(main_camera);
    }
}

std::shared_ptr<camera> world::get_main_camera() const
{
    for (auto& camera : cameras)
    {
        if (camera->is_enabled()) return camera;
    }

    return nullptr;
}

void world::update(double delta_time)
{
    transform.set_camera(get_main_camera());
    transform.set_map(map);

    update_called = true;
}

void world::render(SDL_Renderer* renderer, double delta_time)
{
    if (!update_called)
    {
        std::cout << "WARN: Update wasn't called before the world was rendered! Transform may be invalid as a result." << std::endl;
    }

    unsigned long long render_tile_count = 0;
    unsigned long long iterated_tile_count = 0;

    auto camera = get_main_camera();
    if (!camera) return; // No point in rendering if there is no camera

    SDL_Rect camera_viewport = {
        static_cast<int>(camera->get_viewport_x()),
        static_cast<int>(camera->get_viewport_y()),
        static_cast<int>(camera->get_width()),
        static_cast<int>(camera->get_height())
    };

    // Clip the viewport area so that the diamond edges of the tile map are instead straight lines:
    SDL_RenderSetClipRect(renderer, &camera_viewport);

    unsigned max_tiles_horiz = static_cast<unsigned>(
        /* Add the current camera position */           camera->get_current_x() +
        /* Pixel width of the camera + 1 column */      static_cast<float>(camera->get_width() + map->get_tile_width()) /
        /* Number of half-sized tiles that can fit */   (map->get_tile_width() / 2.0f) + 1);

    unsigned max_tiles_vert = static_cast<unsigned>(
        /* Add the current camera position */           camera->get_current_y() +
        /* Pixel height of the camera + 1 row */        static_cast<float>(camera->get_height() + map->get_tile_height()) /
        /* Number of half-sized tiles that can fit */   (map->get_tile_height() / 2.0f) + 1);

    max_tiles_horiz = std::min(max_tiles_horiz, map->get_map_width());
    max_tiles_vert = std::min(max_tiles_vert, map->get_map_height());

    for (float tile_y = camera->get_current_y(); tile_y < max_tiles_vert; tile_y++)
    {
        for (float tile_x = camera->get_current_x(); tile_x < max_tiles_horiz; tile_x++)
        {
            iterated_tile_count++;

            tile* current_tile = map->get_tile((unsigned)tile_x, (unsigned)tile_y);
            SDL_Point tile_point{ static_cast<int>(tile_x), static_cast<int>(tile_y) };
            std::shared_ptr<tile_image> current_image = nullptr;
            bool is_selected = false;

            // Render image (if there is one) for every layer:
            for (unsigned layer_id = 0; layer_id < map->get_layers().size(); layer_id++)
            {
                if (current_tile && current_tile->has_image(layer_id))
                {
                    // If the tile isn't empty, get it's image:
                    current_image = map->get_image(current_tile->get_image_id(layer_id));
                }
                else if ((!current_tile || !current_tile->has_image(layer_id)) && map->layer_has_default_images(layer_id))
                {
                    // If the tile is empty, attempt to get a default image for the tile and set it so that it's
                    // remembered if this tile comes back into view later.
                    current_image = map->get_image(map->get_random_layer_default_image(layer_id));
                    current_tile->set_image_id(layer_id, current_image->get_image_id());
                }
                else
                {
                    continue; // Tile is definitely empty
                }

                // Tiles are currently in tile coordinates, to render convert it to pixel coordinates relative
                // to the viewport (screen):
                SDL_FPoint screen_pos = transform.world_tile_to_viewport_pixels(tile_point);

                if (current_image != nullptr && current_tile && current_tile->has_image(layer_id))
                {
                    SDL_RenderCopyF(
                        renderer,
                        current_image->get_texture(),
                        current_image->get_source_rect(),   // Where the tile is in the source image
                        current_image->get_dest_rect(
                            screen_pos.x, screen_pos.y,     // Where to actually draw the tile on the screen
                            map->get_tile_height()          // The tile height is used to bottom align tile images
                        )
                    );

                    // For metrics & logging, how many tiles have been rendered?
                    render_tile_count++;
                }

                // Set the currently selected tile based on the position of the mouse cursor:
                if (transform.tile_hittest_by_viewport(screen_pos, input::mouse_position()))
                {
                    set_selection(tile_point);
                    is_selected = true;
                }

                // Render the selection tile if the current tile is selected and this is the first layer:
                if (layer_id == 0 && is_selected && map->has_selection_image())
                {
                    auto selection_image = map->get_selection_image();

                    // The selection tile image should be rendered as semi-transparent
                    SDL_SetTextureAlphaMod(selection_image->get_texture(), 90);

                    SDL_RenderCopyF(
                        renderer,
                        selection_image->get_texture(),
                        selection_image->get_source_rect(),
                        selection_image->get_dest_rect(
                            screen_pos.x, screen_pos.y,
                            map->get_tile_height()
                        )
                    );

                    SDL_SetTextureAlphaMod(selection_image->get_texture(), 255);
                }
            }
        }
    }

    // Render game objects:
    for (const auto& obj : objects)
    {
        if (obj) obj->on_render(renderer, delta_time);
    }

    // Reset clipping so that future rendering isn't affected:
    SDL_RenderSetClipRect(renderer, nullptr);

    // Signal update call checking, after rendering update() will need to be called again. This is primarily for 
    // warning the developer about not calling update() before render()
    update_called = false;
}

void world::set_selection(const SDL_Point& tile_point)
{
    selected_world_tile = tile_point;
}

bool world::has_selection() const
{
    return
        selected_world_tile.x != std::numeric_limits<int>::max() &&
        selected_world_tile.y != std::numeric_limits<int>::max();
}

const SDL_Point& world::get_selection() const
{
    return selected_world_tile;
}

void world::reset_selection()
{
    selected_world_tile.x = std::numeric_limits<int>::max();
    selected_world_tile.y = std::numeric_limits<int>::max();
}

unsigned world::get_max_horizontal_tiles() const
{
    auto camera = get_main_camera();
    unsigned max_tiles_horiz = 0;

    if (camera)
    {
        max_tiles_horiz = camera->get_width() / map->get_tile_width();
    }

    return max_tiles_horiz;
}

unsigned world::get_max_vertical_tiles() const
{
    auto camera = get_main_camera();
    unsigned max_tiles_vert = 0;

    if (camera)
    {
        max_tiles_vert = static_cast<unsigned>(std::round(camera->get_height() / (map->get_tile_height() / 2.0f)));
    }

    return max_tiles_vert;
}

void isometric::world::add_object(std::shared_ptr<game_object> obj)
{
    if (obj)
    {
        obj->setup_transform(get_main_camera(), map);
        objects.push_back(obj);
    }
}

void isometric::world::remove_object(std::shared_ptr<game_object> obj)
{
    if (obj)
    {
        objects.remove(obj);
    }
}

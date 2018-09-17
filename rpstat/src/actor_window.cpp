/**
 * MIT License
 *
 * Copyright (c) 2018 Younguk Kim (bluekyu)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "actor_window.hpp"

#include <paramNodePath.h>

#include <fmt/ostream.h>

#include <render_pipeline/rppanda/showbase/showbase.hpp>
#include <render_pipeline/rpcore/globals.hpp>
#include <render_pipeline/rpcore/render_pipeline.hpp>

#include "rpplugins/rpstat/plugin.hpp"
#include "scenegraph_window.hpp"
#include "load_animation_dialog.hpp"

namespace rpplugins {

ActorWindow::ActorWindow(RPStatPlugin& plugin, rpcore::RenderPipeline& pipeline) : WindowInterface(plugin, pipeline, "Actor Window", "###Actor")
{
    window_flags_ |= ImGuiWindowFlags_MenuBar;

    accept(
        ScenegraphWindow::NODE_SELECTED_EVENT_NAME,
        [this](const Event* ev) {
            auto np = DCAST(ParamNodePath, ev->get_parameter(0).get_ptr())->get_value();

            auto scene_window = plugin_.get_scenegraph_window();
            if (scene_window->is_actor(np))
                set_actor(scene_window->get_actor(np));
            else
                set_actor(nullptr);
        }
    );
}

void ActorWindow::draw_contents()
{
    enum class MenuID : int
    {
        None,
    };
    static MenuID menu_selected = MenuID::None;

    const bool actor_exist = actor_ != nullptr;

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File", actor_exist))
        {
            if (ImGui::MenuItem("Load Animation"))
                load_animation_dialog_ = std::make_unique<LoadAnimationDialog>(plugin_, FileDialog::OperationFlag::open);

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    if (!actor_exist)
        return;

    ui_load_animation();

    static std::vector<rppanda::Actor::PartInfoType>* parts = nullptr;

    {
        static auto getter = [](void* data, int idx, const char** out_text) {
            auto plugin = reinterpret_cast<ActorWindow*>(data);
            *out_text = std::get<0>(plugin->actor_info_[idx]).c_str();
            return true;
        };

        static int current_item = -1;
        const int combo_size = static_cast<int>(actor_info_.size());
        current_item = (std::min)(current_item, combo_size - 1);

        ImGui::Combo("Level of Details", &current_item, getter, this, combo_size);

        parts = current_item != -1 ? &std::get<1>(actor_info_[current_item]) : nullptr;
    }

    static PartBundle* part_bundle = nullptr;
    static std::vector<rppanda::Actor::AnimInfoType>* anims = nullptr;
    {
        static auto getter = [](void* data, int idx, const char** out_text) {
            auto p = reinterpret_cast<decltype(parts)>(data);
            *out_text = std::get<0>((*p)[idx]).c_str();
            return true;
        };

        static int current_item = -1;
        const int combo_size = parts ? static_cast<int>(parts->size()) : 0;
        current_item = (std::min)(current_item, combo_size - 1);

        ImGui::Combo("Parts", &current_item, getter, parts, combo_size);

        part_bundle = current_item != -1 ? std::get<1>((*parts)[current_item]) : nullptr;
        anims = current_item != -1 ? &std::get<2>((*parts)[current_item]) : nullptr;
    }

    // part bundle
    {
    }

    static std::string* filename = nullptr;
    static AnimControl* anim_control = nullptr;
    {
        static auto getter = [](void* data, int idx, const char** out_text) {
            auto p = reinterpret_cast<decltype(anims)>(data);
            *out_text = std::get<0>((*p)[idx]).c_str();
            return true;
        };

        static int current_item = -1;
        const int combo_size = anims ? static_cast<int>(anims->size()) : 0;
        current_item = (std::min)(current_item, combo_size - 1);

        ImGui::Combo("Anims", &current_item, getter, anims, combo_size);

        filename = current_item != -1 ? &std::get<1>((*anims)[current_item]) : nullptr;
        anim_control = current_item != -1 ? std::get<2>((*anims)[current_item]) : nullptr;
    }
}

void ActorWindow::set_actor(rppanda::Actor* actor)
{
    actor_ = actor;
    if (!actor_)
        return;

    actor_updated();
}

void ActorWindow::ui_load_animation()
{
    if (!(load_animation_dialog_ && load_animation_dialog_->draw()))
        return;

    const auto& fname = load_animation_dialog_->get_filename();
    if (fname && !fname->empty())
    {
        try
        {
            actor_->load_anims({ { load_animation_dialog_->get_animation_name(), *fname} });

            actor_updated();
        }
        catch (const std::exception& err)
        {
            plugin_.error(err.what());
        }
    }

    load_animation_dialog_.reset();
}

void ActorWindow::actor_updated()
{
    actor_info_ = actor_->get_actor_info();
}

}

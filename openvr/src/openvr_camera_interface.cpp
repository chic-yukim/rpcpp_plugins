/**
 * MIT License
 *
 * Copyright (c) 2017 Center of Human-centered Interaction for Coexistence
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

#include "openvr_camera_interface.hpp"

#include <matrixLens.h>

#include <render_pipeline/rpcore/globals.hpp>
#include <render_pipeline/rppanda/showbase/showbase.hpp>

namespace rpplugins {

PT(Camera) OpenVRCameraInterface::create_camera_node(const std::string& name,
    const LVecBase2f& near_far, bool use_openvr_projection,
    vr::EVRTrackedCameraFrameType frame_type) const
{
    const auto vr_lens = rpcore::Globals::base->get_cam_lens();
    LVecBase2f vr_near_far;
    vr_near_far[0] = near_far[0] <= 0 ? vr_lens->get_near() : near_far[0];
    vr_near_far[1] = near_far[1] <= 0 ? vr_lens->get_far() : near_far[1];

    PT(Camera) cam;
    if (use_openvr_projection)
    {
        LMatrix4f proj_mat;
        if (get_projection(vr_near_far, proj_mat, frame_type) != vr::VRTrackedCameraError_None)
        {
            plugin_.error("Failed to get projection matrix.");
            return nullptr;
        }

        PT(MatrixLens) lens = new MatrixLens;

        // OpenGL film (NDC) is [-1, 1] on zero origin.
        lens->set_film_size(2, 2);
        lens->set_user_mat(
            LMatrix4f::convert_mat(CS_zup_right, CS_yup_right) * proj_mat);
        cam = new Camera(name, lens);
    }
    else
    {
        uint32_t width;
        uint32_t height;
        uint32_t buffer_size;
        if (get_frame_size(width, height, buffer_size, frame_type) != vr::VRTrackedCameraError_None)
        {
            plugin_.error("Failed to get camera frame size.");
            return nullptr;
        }

        LVecBase2f focal_length;
        LVecBase2f center;
        if (get_intrinsics(focal_length, center, frame_type) != vr::VRTrackedCameraError_None)
        {
            plugin_.error("Failed to get camera intrinsic.");
            return nullptr;
        }

        if (focal_length[0] != focal_length[1])
            plugin_.warn("X and Y of focal length are NOT same.");

        PerspectiveLens* lens = new PerspectiveLens;
        lens->set_near_far(vr_near_far[0], vr_near_far[1]);
        lens->set_film_size(width, height);
        lens->set_focal_length(focal_length[0]);
        lens->set_film_offset(width / 2.0f - center[0], center[1] - height / 2.0f);
        cam = new Camera(name, lens);
    }

    return cam;
}

}
/**
 * The MIT License (MIT)
 * 
 * Copyright (c) 2016, Center of human-centered interaction for coexistence.
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

#include "rpflex/plugin.hpp"

#include <clockObject.h>

#include <boost/dll/alias.hpp>

#include <NvFlex.h>
#include <NvFlexDevice.h>

#include <render_pipeline/rpcore/globals.h>
#include <render_pipeline/rppanda/showbase/showbase.h>

#include "rpflex/flex_buffer.hpp"
#include "rpflex/instance_interface.hpp"

RENDER_PIPELINE_PLUGIN_CREATOR(rpflex::Plugin)

namespace rpflex {

struct Plugin::Impl
{
    Impl(Plugin& self);

    void destroy(void);
    void reset(void);

    void on_pipeline_created(void);
    void on_pre_render_update(void);
    void on_post_render_update(void);
    void on_unload(void);

    static RequrieType require_plugins_;

    Plugin& self_;

    NvFlexLibrary* library_ = nullptr;
    FlexBuffer* buffer_ = nullptr;
    NvFlexSolver* solver_ = nullptr;

    NvFlexParams params_;
    bool params_changed_ = false;

    uint32_t max_particles_ = 30000;
    const int substeps_ = 2;

    std::vector<std::shared_ptr<InstanceInterface>> instances_;
};

Plugin::RequrieType Plugin::Impl::require_plugins_;

Plugin::Impl::Impl(Plugin& self): self_(self)
{
}

void Plugin::Impl::destroy(void)
{
    self_.trace("Destroy flex.");

    if (buffer_)
    {
        buffer_->destroy();
        delete buffer_;
        buffer_ = nullptr;
    }

    if (solver_)
    {
        NvFlexDestroySolver(solver_);
        solver_ = nullptr;
    }
}

void Plugin::Impl::reset(void)
{
    self_.trace("Reset flex.");

    if (solver_)
        destroy();

    self_.trace("Creating flex buffer.");

    // alloc buffers
    buffer_ = new FlexBuffer(library_);

    // map during initialization
    buffer_->map();

    buffer_->positions_.resize(0);
    buffer_->velocities_.resize(0);
    buffer_->phases_.resize(0);

    self_.trace("Setup simulation parameters.");

    // sim params
    params_.gravity[0] = 0.0f;
    params_.gravity[1] = 0.0f;
    params_.gravity[2] = -9.8f;

    params_.wind[0] = 0.0f;
    params_.wind[1] = 0.0f;
    params_.wind[2] = 0.0f;

    params_.radius = 0.2f;
    params_.viscosity = 0.0f;
    params_.dynamicFriction = 0.5f;
    params_.staticFriction = 1.0f;
    params_.particleFriction = 0.0f; // scale friction between particles by default
    params_.freeSurfaceDrag = 0.0f;
    params_.drag = 0.0f;
    params_.lift = 0.0f;
    params_.numIterations = 3;
    params_.fluidRestDistance = 0.0f;
    params_.solidRestDistance = 0.0f;

    params_.anisotropyScale = 1.0f;
    params_.anisotropyMin = 0.1f;
    params_.anisotropyMax = 2.0f;
    params_.smoothing = 1.0f;

    params_.dissipation = 0.0f;
    params_.damping = 0.0f;
    params_.particleCollisionMargin = params_.radius*0.25f;
    params_.shapeCollisionMargin =  params_.radius*0.25f;
    params_.collisionDistance = 0.0f;
    params_.plasticThreshold = 0.0f;
    params_.plasticCreep = 0.0f;
    params_.fluid = false;
    params_.sleepThreshold = 0.0f;
    params_.shockPropagation = 0.0f;
    params_.restitution = 0.0f;

    params_.maxSpeed = FLT_MAX;
    params_.maxAcceleration = 100.0f;	// approximately 10x gravity

    params_.relaxationMode = eNvFlexRelaxationLocal;
    params_.relaxationFactor = 1.0f;
    params_.solidPressure = 1.0f;
    params_.adhesion = 0.0f;
    params_.cohesion = 0.025f;
    params_.surfaceTension = 0.0f;
    params_.vorticityConfinement = 0.0f;
    params_.buoyancy = 1.0f;
    params_.diffuseThreshold = 100.0f;
    params_.diffuseBuoyancy = 1.0f;
    params_.diffuseDrag = 0.8f;
    params_.diffuseBallistic = 16;
    params_.diffuseSortAxis[0] = 0.0f;
    params_.diffuseSortAxis[1] = 0.0f;
    params_.diffuseSortAxis[2] = 0.0f;
    params_.diffuseLifetime = 2.0f;

    // planes created after particles
    params_.numPlanes = 1;

    // create scene
    for (auto& instance: instances_)
        instance->initialize(*buffer_);
    uint32_t num_particles = buffer_->positions_.size();

    // by default solid particles use the maximum radius
    if (params_.fluid && params_.solidRestDistance == 0.0f)
        params_.solidRestDistance = params_.fluidRestDistance;
    else
        params_.solidRestDistance = params_.radius;

    // collision distance with shapes half the radius
    if (params_.collisionDistance == 0.0f)
    {
        params_.collisionDistance = params_.radius*0.5f;

        if (params_.fluid)
            params_.collisionDistance = params_.fluidRestDistance*0.5f;
    }

    // default particle friction to 10% of shape friction
    if (params_.particleFriction == 0.0f)
        params_.particleFriction = params_.dynamicFriction*0.1f;

    // add a margin for detecting contacts between particles and shapes
    if (params_.shapeCollisionMargin == 0.0f)
        params_.shapeCollisionMargin = params_.collisionDistance*0.5f;

    // update collision planes to match flexs
    reinterpret_cast<LVecBase4f&>(params_.planes[0]) = LVecBase4f(0.0f, 0.0f, 1.0f, 0.0f);

    self_.trace("Creating solver.");

    // main create method for the Flex solver
    solver_ = NvFlexCreateSolver(library_, max_particles_, 0);

    // create active indices (just a contiguous block for the demo)
    buffer_->active_indices_.resize(buffer_->positions_.size());
    for (int i = 0; i < buffer_->active_indices_.size(); ++i)
        buffer_->active_indices_[i] = i;

    // resize particle buffers to fit
    buffer_->positions_.resize(max_particles_);
    buffer_->velocities_.resize(max_particles_);
    buffer_->phases_.resize(max_particles_);

    // unmap so we can start transferring data to GPU
    buffer_->unmap();

    self_.trace("Sending data.");

    // Send data to Flex
    NvFlexSetParams(solver_, &params_);
    NvFlexSetParticles(solver_, buffer_->positions_.buffer, num_particles);
    NvFlexSetVelocities(solver_, buffer_->velocities_.buffer, num_particles);
    NvFlexSetPhases(solver_, buffer_->phases_.buffer, buffer_->phases_.size());

    NvFlexSetActive(solver_, buffer_->active_indices_.buffer, num_particles);

    // collision shapes
    if (buffer_->shape_flags_.size())
    {
        NvFlexSetShapes(
            solver_,
            buffer_->shape_geometry_.buffer,
            buffer_->shape_positions_.buffer,
            buffer_->shape_rotations_.buffer,
            buffer_->shape_prev_positions_.buffer,
            buffer_->shape_prev_rotations_.buffer,
            buffer_->shape_flags_.buffer,
            int(buffer_->shape_flags_.size()));
    }
}

void Plugin::Impl::on_pipeline_created(void)
{
    rpcore::Globals::base->add_task([](GenericAsyncTask *task, void *user_data) {
        reinterpret_cast<Plugin::Impl*>(user_data)->reset();
        return AsyncTask::DS_done;
    }, this, "Plugin::reset");
}

void Plugin::Impl::on_pre_render_update(void)
{
    // Scene Update
    buffer_->map();

    for (auto& instance: instances_)
        instance->sync_flex(*buffer_);

    // unmap buffers
    buffer_->unmap();
}

void Plugin::Impl::on_post_render_update(void)
{
    // send any particle updates to the solver
    NvFlexSetParticles(solver_, buffer_->positions_.buffer, buffer_->positions_.size());
    NvFlexSetVelocities(solver_, buffer_->velocities_.buffer, buffer_->velocities_.size());
    NvFlexSetPhases(solver_, buffer_->phases_.buffer, buffer_->phases_.size());
    NvFlexSetActive(solver_, buffer_->active_indices_.buffer, buffer_->active_indices_.size());

    // tick solver
    if (params_changed_)
    {
        NvFlexSetParams(solver_, &params_);
        params_changed_ = false;
    }
    NvFlexUpdateSolver(solver_, float(ClockObject::get_global_clock()->get_dt()), substeps_, false);

    // read back base particle data
    // Note that flexGet calls don't wait for the GPU, they just queue a GPU copy 
    // to be executed later.
    // When we're ready to read the fetched buffers we'll Map them, and that's when
    // the CPU will wait for the GPU flex update and GPU copy to finish.
    NvFlexGetParticles(solver_, buffer_->positions_.buffer, buffer_->positions_.size());
    NvFlexGetVelocities(solver_, buffer_->velocities_.buffer, buffer_->velocities_.size());
}

void Plugin::Impl::on_unload(void)
{
    destroy();

    if (library_)
        NvFlexShutdown(library_);
}

// ************************************************************************************************

Plugin::Plugin(rpcore::RenderPipeline& pipeline): BasePlugin(pipeline, RPPLUGIN_ID_STRING), impl_(std::make_unique<Impl>(*this))
{
}

Plugin::~Plugin(void) = default;

Plugin::RequrieType& Plugin::get_required_plugins(void) const
{
    return impl_->require_plugins_;
}

void Plugin::on_load(void)
{
    // use the PhysX GPU selected from the NVIDIA control panel
    int device_index = NvFlexDeviceGetSuggestedOrdinal();

    // Create an optimized CUDA context for Flex and set it on the 
    // calling thread. This is an optional call, it is fine to use 
    // a regular CUDA context, although creating one through this API
    // is recommended for best performance.
    bool success = NvFlexDeviceCreateCudaContext(device_index);

    if (!success)
    {
        error("Error creating CUDA context.");
        return;
    }

    NvFlexInitDesc desc;
    desc.deviceIndex = device_index;
    desc.enableExtensions = true;
    desc.renderDevice = 0;
    desc.renderContext = 0;
    desc.computeType = eNvFlexCUDA;

    // Init Flex library, note that no CUDA methods should be called before this 
    // point to ensure we get the device context we want
    impl_->library_ = NvFlexInit(NV_FLEX_VERSION, [](NvFlexErrorSeverity, const char* msg, const char* file, int line) {
        RPObject::global_error(RPPLUGIN_ID_STRING, std::string(msg) + " - " + std::string(file) + ":" + std::to_string(line));
    }, &desc);

    if (!impl_->library_)
    {
        error("Could not initialize Flex, exiting.\n");
        return;
    }

    // store device name
    info(std::string("Compute Device: ") + NvFlexGetDeviceName(impl_->library_));
}

void Plugin::on_stage_setup(void)
{
}

void Plugin::on_pipeline_created(void)
{
    impl_->on_pipeline_created();
}

void Plugin::on_pre_render_update(void)
{
    impl_->on_pre_render_update();
}

void Plugin::on_post_render_update(void)
{
    impl_->on_post_render_update();
}

void Plugin::on_unload(void)
{
    impl_->on_unload();
}

void Plugin::add_instance(const std::shared_ptr<InstanceInterface>& instance)
{
    impl_->instances_.push_back(instance);
}

NvFlexLibrary* Plugin::get_flex_library(void) const
{
    return impl_->library_;
}

NvFlexSolver* Plugin::get_flex_solver(void) const
{
    return impl_->solver_;
}

const NvFlexParams& Plugin::get_flex_params(void) const
{
    return impl_->params_;
}

NvFlexParams& Plugin::modify_flex_params(void)
{
    impl_->params_changed_ = true;
    return impl_->params_;
}

}
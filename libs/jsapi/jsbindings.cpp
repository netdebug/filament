/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * DESIGN
 * ------
 *
 * The purpose of the jsapi library is to offer a first-class JavaScript interface to the core
 * Filament classes: Engine, Renderer, Texture, etc.
 *
 * Emscripten offers two ways to binding JavaScript to C++: embind and WebIDL. We chose embind.
 *
 * With WebIDL, we would need to author WebIDL files and generate opaque C++ from the IDL, which
 * complicates the build process and ultimately results in the same amount of code. Using embind is
 * more direct and controllable.
 *
 * For nested classes, we use $ as the separator character because embind does not support nested
 * classes and it would transform dot separators into $ anyway. By using $ here, we at least make
 * this explicit rather than mysterious.
 */

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <utils/EntityManager.h>

#include <emscripten/bind.h>

using namespace emscripten;
using namespace filament;

namespace {

// For convenience, declare terse private aliases to nested types. This lets us avoid extremely
// verbose binding declarations.
using RenderBuilder = RenderableManager::Builder;
using VertexBuilder = VertexBuffer::Builder;
using IndexBuilder = IndexBuffer::Builder;

}

// Individual JavaScript objects for math types would be too heavy, so instead we simply accept
// array-like data using embind's "value_array" feature. We do not expose all our math functions
// under the assumption that JS clients will use glMatrix or something similar for math.

EMSCRIPTEN_BINDINGS(array_types) {

    value_array<math::float2>("float2")
        .element(&math::float2::x)
        .element(&math::float2::y);

    value_array<math::float3>("float3")
        .element(&math::float3::x)
        .element(&math::float3::y)
        .element(&math::float3::z);

    value_array<math::float4>("float4")
        .element(&math::float4::x)
        .element(&math::float4::y)
        .element(&math::float4::z)
        .element(&math::float4::w);

    value_array<Box>("Box")
        .element(&Box::center)
        .element(&Box::halfExtent);

}

EMSCRIPTEN_BINDINGS(constants) {

 enum_<VertexAttribute>("VertexAttribute")
        .value("POSITION", POSITION)
        .value("TANGENTS", TANGENTS)
        .value("COLOR", COLOR)
        .value("UV0", UV0)
        .value("UV1", UV1)
        .value("BONE_INDICES", BONE_INDICES)
        .value("BONE_WEIGHTS", BONE_WEIGHTS);

 enum_<VertexBuffer::AttributeType>("VertexBuffer$AttributeType")
        .value("BYTE", VertexBuffer::AttributeType::BYTE)
        .value("BYTE2", VertexBuffer::AttributeType::BYTE2)
        .value("BYTE3", VertexBuffer::AttributeType::BYTE3)
        .value("BYTE4", VertexBuffer::AttributeType::BYTE4)
        .value("UBYTE", VertexBuffer::AttributeType::UBYTE)
        .value("UBYTE2", VertexBuffer::AttributeType::UBYTE2)
        .value("UBYTE3", VertexBuffer::AttributeType::UBYTE3)
        .value("UBYTE4", VertexBuffer::AttributeType::UBYTE4)
        .value("SHORT", VertexBuffer::AttributeType::SHORT)
        .value("SHORT2", VertexBuffer::AttributeType::SHORT2)
        .value("SHORT3", VertexBuffer::AttributeType::SHORT3)
        .value("SHORT4", VertexBuffer::AttributeType::SHORT4)
        .value("USHORT", VertexBuffer::AttributeType::USHORT)
        .value("USHORT2", VertexBuffer::AttributeType::USHORT2)
        .value("USHORT3", VertexBuffer::AttributeType::USHORT3)
        .value("USHORT4", VertexBuffer::AttributeType::USHORT4)
        .value("INT", VertexBuffer::AttributeType::INT)
        .value("UINT", VertexBuffer::AttributeType::UINT)
        .value("FLOAT", VertexBuffer::AttributeType::FLOAT)
        .value("FLOAT2", VertexBuffer::AttributeType::FLOAT2)
        .value("FLOAT3", VertexBuffer::AttributeType::FLOAT3)
        .value("FLOAT4", VertexBuffer::AttributeType::FLOAT4)
        .value("HALF", VertexBuffer::AttributeType::HALF)
        .value("HALF2", VertexBuffer::AttributeType::HALF2)
        .value("HALF3", VertexBuffer::AttributeType::HALF3)
        .value("HALF4", VertexBuffer::AttributeType::HALF4);

 enum_<RenderableManager::PrimitiveType>("RenderableManager$PrimitiveType")
        .value("POINTS", RenderableManager::PrimitiveType::POINTS)
        .value("LINES", RenderableManager::PrimitiveType::LINES)
        .value("TRIANGLES", RenderableManager::PrimitiveType::TRIANGLES)
        .value("NONE", RenderableManager::PrimitiveType::NONE);
}

EMSCRIPTEN_BINDINGS(core_types) {

    // Engine
    class_<Engine>("Engine")

        .class_function("create", (Engine* (*)()) [] () { return Engine::create(); },
                allow_raw_pointers())
        .class_function("destroy", (void (*)(Engine*)) []
                (Engine* engine) { Engine::destroy(&engine); }, allow_raw_pointers())

        .class_function("destroy", &Engine::destroy, allow_raw_pointers())

        .function("createSwapChain", (SwapChain* (*)(Engine*)) []
                (Engine* engine) { return engine->createSwapChain(nullptr); },
                allow_raw_pointers())
        .function("destroySwapChain", (void (*)(Engine*, SwapChain*)) []
                (Engine* engine, SwapChain* swapChain) { engine->destroy(swapChain); },
                allow_raw_pointers())

        .function("createRenderer", &Engine::createRenderer, allow_raw_pointers())
        .function("destroyRenderer", (void (*)(Engine*, Renderer*)) []
                (Engine* engine, Renderer* renderer) { engine->destroy(renderer); },
                allow_raw_pointers())

        .function("createView", &Engine::createView, allow_raw_pointers())
        .function("destroyView", (void (*)(Engine*, View*)) []
                (Engine* engine, View* view) { engine->destroy(view); },
                allow_raw_pointers())

        .function("createScene", &Engine::createScene, allow_raw_pointers())
        .function("destroyScene", (void (*)(Engine*, Scene*)) []
                (Engine* engine, Scene* scene) { engine->destroy(scene); },
                allow_raw_pointers())

        .function("createCamera", select_overload<Camera*(void)>(&Engine::createCamera),
                allow_raw_pointers())
        .function("destroyCamera", (void (*)(Engine*, Camera*)) []
                (Engine* engine, Camera* camera) { engine->destroy(camera); },
                allow_raw_pointers())

        .function("destroyEntity", (void (*)(Engine*, utils::Entity)) []
                (Engine* engine, utils::Entity entity) { engine->destroy(entity); },
                allow_raw_pointers())

        .function("destroyVertexBuffer", (void (*)(Engine*, VertexBuffer*)) []
                (Engine* engine, VertexBuffer* vb) { engine->destroy(vb); },
                allow_raw_pointers());

    // SwapChain
    class_<SwapChain>("SwapChain");

    // Renderer
    class_<Renderer>("Renderer")
        .function("render", &Renderer::render, allow_raw_pointers());

    // View
    class_<View>("View")
        .function("setScene", &View::setScene, allow_raw_pointers())
        .function("setCamera", &View::setCamera, allow_raw_pointers());

    // Scene
    class_<Scene>("Scene")
        .function("addEntity", &Scene::addEntity);

    // Camera
    class_<Camera>("Camera");

    // RenderableManager
    class_<RenderBuilder>("RenderableManager$Builder")

        .function("build", (void (*)(RenderBuilder*, Engine*, utils::Entity)) []
                (RenderBuilder* builder, Engine* engine, utils::Entity entity) {
            builder->build(*engine, entity);
        }, allow_raw_pointers())

        .function("boundingBox", (RenderBuilder* (*)(RenderBuilder*, Box)) []
                (RenderBuilder* builder, Box box) {
            return &builder->boundingBox(box);
        }, allow_raw_pointers())

        .function("culling", (RenderBuilder* (*)(RenderBuilder*, bool)) []
                (RenderBuilder* builder, bool enable) {
            return &builder->culling(enable);
        }, allow_raw_pointers())

        .function("receiveShadows", (RenderBuilder* (*)(RenderBuilder*, bool)) []
                (RenderBuilder* builder, bool enable) {
            return &builder->receiveShadows(enable);
        }, allow_raw_pointers())

        .function("castShadows", (RenderBuilder* (*)(RenderBuilder*, bool)) []
                (RenderBuilder* builder, bool enable) {
            return &builder->castShadows(enable);
        }, allow_raw_pointers());

    class_<RenderableManager>("RenderableManager")
        .class_function("Builder", (RenderBuilder (*)(int)) []
            (int n) { return RenderBuilder(n); });

    // VertexBuffer
    class_<VertexBuilder>("VertexBuffer$Builder")

        .function("build", (void (*)(VertexBuilder*, Engine*)) []
                (VertexBuilder* builder, Engine* engine) {
            builder->build(*engine);
        }, allow_raw_pointers())

        .function("vertexCount", (VertexBuilder* (*)(VertexBuilder*, int)) []
                (VertexBuilder* builder, int count) {
            return &builder->vertexCount(count);
        }, allow_raw_pointers())

        .function("normalized", (VertexBuilder* (*)(VertexBuilder*, VertexAttribute)) []
                (VertexBuilder* builder, VertexAttribute attrib) {
            return &builder->normalized(attrib);
        }, allow_raw_pointers())

        .function("bufferCount", (VertexBuilder* (*)(VertexBuilder*, int)) []
                (VertexBuilder* builder, int count) {
            return &builder->bufferCount(count);
        }, allow_raw_pointers());

    class_<VertexBuffer>("VertexBuffer")
        .class_function("Builder", (VertexBuilder (*)()) [] () { return VertexBuilder(); });

    // IndexBuffer
    class_<IndexBuilder>("IndexBuffer$Builder")
        .function("build", (void (*)(IndexBuilder*, Engine*)) []
                (IndexBuilder* builder, Engine* engine) {
            builder->build(*engine);
        }, allow_raw_pointers());

    class_<IndexBuffer>("IndexBuffer")
        .class_function("Builder", (IndexBuilder (*)()) [] () { return IndexBuilder(); });

}

EMSCRIPTEN_BINDINGS(utils) {

    // Entity
    class_<utils::Entity>("Entity");

    // EntityManager
    class_<utils::EntityManager>("EntityManager")
        .class_function("get", (utils::EntityManager* (*)()) []
            () { return &utils::EntityManager::get(); }, allow_raw_pointers())
        .function("create", select_overload<utils::Entity()>(&utils::EntityManager::create))
        .function("destroy", select_overload<void(utils::Entity)>(&utils::EntityManager::destroy));
}

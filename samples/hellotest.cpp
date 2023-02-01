#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/LightManager.h>

#include <utils/EntityManager.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>
#include <filamentapp/IcoSphere.h>

#include <geometry/SurfaceOrientation.h>

#include <cmath>

#include "generated/resources/resources.h"

using namespace filament;
using utils::Entity;
using utils::EntityManager;
using namespace filament::math;
using namespace filament::geometry;

using ResourcePair = std::pair<const uint8_t*, size_t>;

const ResourcePair BAKED_COLOR {RESOURCES_BAKEDCOLOR_DATA, RESOURCES_BAKEDCOLOR_SIZE};
const ResourcePair SANDBOXLIT {RESOURCES_SANDBOXLIT_DATA, RESOURCES_SANDBOXLIT_SIZE};

struct App {
    VertexBuffer* vb;
    IndexBuffer* ib;
    Material* mat;
    MaterialInstance* matInst;
    Camera* cam;
    Entity camera;
    Skybox* skybox;
    Entity renderable;
    Entity light;
};

struct Vertex {
    float3 position;
    uint32_t color;
    quatf ts;
};

uint32_t vec3ToColor(const float3& vec) {
    uint32_t out = 0xff000000;
    out |= (uint32_t) ((vec.r + 1.f) / 2.f) * 255;
    out |= ((uint32_t) (((vec.g + 1.f) / 2.f) * 255)) << 8;
    out |= ((uint32_t) (((vec.b + 1.f) / 2.f) * 255)) << 16;
    return out;
}

void quatTo(const quatf& quat, float3* normal, float3* tangent) {
    mat3 m{quat};
    *normal = m[2];
    *tangent = m[1];
}

int main(int argc, char** argv) {
    Config config;
    config.title = "hellotest";

    const size_t vertexStride = sizeof(Vertex);

    IcoSphere sphere(3);
    auto& spPositions = sphere.getVertices();
    auto& spIndices = sphere.getIndices();
    auto& triangles = spIndices;

    const size_t vertexCount = spPositions.size();
    const size_t triangleCount = spIndices.size();

    std::vector<Vertex> vertices(vertexCount);

    auto tangents = SurfaceOrientation::Builder()
        .vertexCount(vertexCount)
        .positions(spPositions.data())
        .triangleCount(triangleCount)
        .triangles((const ushort3*) spIndices.data())
        .build();

    tangents->getQuats(
            (quatf*) (((uint8_t*) vertices.data()) + sizeof(float3) + sizeof(uint32_t)),
            vertexCount, vertexStride);

    for (size_t i = 0; i < vertexCount; i++) {
        vertices[i].position = spPositions[i];
        float3 normal, tangent;
        quatTo(vertices[i].ts, &normal, &tangent);
        vertices[i].color = vec3ToColor(tangent);
    }

    App app;
    auto setup = [&app, &sphere, &triangleCount, &vertexCount, &vertices, &triangles]
        (Engine* engine, View* view, Scene* scene) {
        app.skybox = Skybox::Builder().color({0.1, 0.125, 0.25, 1.0}).build(*engine);
        scene->setSkybox(app.skybox);
        view->setPostProcessingEnabled(false);

        app.vb = VertexBuffer::Builder()
                .vertexCount(vertexCount)
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, 0,
                           vertexStride)
                .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::UBYTE4, 12,
                           vertexStride)
                .normalized(VertexAttribute::COLOR)
                .attribute(VertexAttribute::TANGENTS, 0, VertexBuffer::AttributeType::FLOAT4, 16,
                           vertexStride)
                .build(*engine);
        app.vb->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(vertices.data(), vertexStride * vertexCount));
        app.ib = IndexBuffer::Builder()
                .indexCount(triangleCount * 3)
                .bufferType(IndexBuffer::IndexType::USHORT)
                .build(*engine);
        app.ib->setBuffer(*engine,
                IndexBuffer::BufferDescriptor(triangles.data(), sizeof(ushort3) * triangleCount));

        app.light = utils::EntityManager::get().create();
        LightManager::Builder(LightManager::Type::POINT)
                .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
                .intensity(1000.0f, LightManager::EFFICIENCY_LED)
                .position({ 0.0f, 0.0f, -3.0f })
                .lightChannel(0)
                .build(*engine, app.light);
        scene->addEntity(app.light);

        auto package = BAKED_COLOR;
        //auto package = SANDBOXLIT;

        app.mat = Material::Builder()
            .package(package.first, package.second)
            .build(*engine);
        app.matInst = app.mat->createInstance();
        if (package == SANDBOXLIT) {
            app.matInst->setParameter("baseColor", RgbType::LINEAR, float3{0.8});
            app.matInst->setParameter("metallic", 1.0f);
            app.matInst->setParameter("roughness", 0.4f);
            app.matInst->setParameter("reflectance", 0.5f);
        }

        app.renderable = EntityManager::get().create();
        RenderableManager::Builder(1)
                .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})
                .material(0, app.matInst)
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib, 0, triangleCount * 3)
                .build(*engine, app.renderable);

        scene->addEntity(app.renderable);
        app.camera = utils::EntityManager::get().create();
        app.cam = engine->createCamera(app.camera);

        const uint32_t w = view->getViewport().width;
        const uint32_t h = view->getViewport().height;
        const float aspect = (float) w / h;
        app.cam->setLensProjection(28.0f, aspect, 0.1, 100);
        app.cam->lookAt(float3(0, 0, 4.5f), float3(0, 0, 0), float3(0, 1.f, 0));

        view->setCamera(app.cam);
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.skybox);
        engine->destroy(app.renderable);
        engine->destroy(app.light);
        engine->destroy(app.mat);
        engine->destroy(app.matInst);
        engine->destroy(app.vb);
        engine->destroy(app.ib);
        engine->destroyCameraComponent(app.camera);
        utils::EntityManager::get().destroy(app.camera);
    };

    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        auto& tcm = engine->getTransformManager();
        tcm.setTransform(tcm.getInstance(app.renderable),
                filament::math::mat4f::rotation(now, filament::math::float3{ 0, 1, 0 }));
    });

    FilamentApp::get().run(config, setup, cleanup);

    return 0;
}

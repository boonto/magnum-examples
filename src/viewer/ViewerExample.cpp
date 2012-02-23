/*
    Copyright © 2010, 2011, 2012 Vladimír Vondruš <mosra@centrum.cz>

    This file is part of Magnum.

    Magnum is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License version 3
    only, as published by the Free Software Foundation.

    Magnum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License version 3 for more details.
*/

#include <iostream>
#include <chrono>
#include <memory>

#include "PluginManager/PluginManager.h"
#include "Scene.h"
#include "Camera.h"
#include "Trade/AbstractImporter.h"
#include "Trade/MeshData.h"
#include "MeshTools/Tipsify.h"
#include "Shaders/PhongShader.h"

#include "AbstractExample.h"
#include "ViewedObject.h"
#include "configure.h"

using namespace std;
using namespace Corrade::PluginManager;
using namespace Corrade::Utility;
using namespace Magnum;
using namespace Magnum::Shaders;
using namespace Magnum::Examples;

namespace Magnum { namespace Examples {

class ViewerExample: public AbstractExample {
    public:
        ViewerExample(int& argc, char** argv): AbstractExample(argc, argv, "Magnum Viewer"), wireframe(false), fps(false) {
            if(argc != 2) {
                cout << "Usage: " << argv[0] << " file.dae" << endl;
                exit(0);
            }

            /* Instance ColladaImporter plugin */
            PluginManager<Trade::AbstractImporter> manager(PLUGIN_IMPORTER_DIR);
            if(manager.load("ColladaImporter") != AbstractPluginManager::LoadOk) {
                Error() << "Could not load ColladaImporter plugin";
                exit(1);
            }
            unique_ptr<Trade::AbstractImporter> colladaImporter(manager.instance("ColladaImporter"));
            if(!colladaImporter) {
                Error() << "Could not instance ColladaImporter plugin";
                exit(2);
            }
            if(!(colladaImporter->features() & Trade::AbstractImporter::OpenFile)) {
                Error() << "ColladaImporter cannot open files";
                exit(3);
            }

            scene.setFeature(Scene::DepthTest, true);

            /* Every scene needs a camera */
            camera = new Camera(&scene);
            camera->setPerspective(deg(35.0f), 0.001f, 100);
            camera->translate(0, 0, 5);

            /* Load file */
            if(!colladaImporter->open(argv[1]))
                exit(4);

            if(colladaImporter->meshCount() == 0)
                exit(5);

            Trade::MeshData* data = colladaImporter->mesh(0);

            /* Optimize vertices */
            if(data && data->indices() && data->vertexArrayCount() == 1) {
                Debug() << "Optimizing mesh vertices using Tipsify algorithm (cache size 24)...";
                MeshTools::tipsify(*data->indices(), data->vertices(0)->size(), 24);
            } else exit(6);

            /* Interleave mesh data */
            struct VertexData {
                Vector4 vertex;
                Vector3 normal;
            };
            vector<VertexData> interleavedMeshData;
            interleavedMeshData.reserve(data->vertices(0)->size());
            for(size_t i = 0; i != data->vertices(0)->size(); ++i) {
                interleavedMeshData.push_back({
                    (*data->vertices(0))[i],
                    (*data->normals(0))[i]
                });
            }
            MeshBuilder<VertexData> builder;
            builder.setData(interleavedMeshData.data(), data->indices()->data(), interleavedMeshData.size(), data->indices()->size());

            Buffer* buffer = mesh.addBuffer(true);
            mesh.bindAttribute<Vector4>(buffer, PhongShader::Vertex);
            mesh.bindAttribute<Vector3>(buffer, PhongShader::Normal);
            builder.build(&mesh, buffer, Buffer::Usage::StaticDraw, Buffer::Usage::StaticDraw);

            o = new ViewedObject(&mesh, static_cast<Trade::PhongMaterialData*>(colladaImporter->material(0)), &shader, &scene);

            colladaImporter->close();
            delete colladaImporter.release();
        }

    protected:
        inline void viewportEvent(const Math::Vector2<GLsizei>& size) {
            camera->setViewport(size);
        }

        void drawEvent() {
            if(fps) {
                chrono::high_resolution_clock::time_point now = chrono::high_resolution_clock::now();
                double duration = chrono::duration<double>(now-before).count();
                if(duration > 3.5) {
                    cout << frames << " frames in " << duration << " sec: "
                        << frames/duration << " FPS         \r";
                    cout.flush();
                    totalfps += frames/duration;
                    before = now;
                    frames = 0;
                    ++totalmeasurecount;
                }
            }
            camera->draw();
            swapBuffers();

            if(fps) {
                ++frames;
                redraw();
            }
        }

        void keyEvent(Key key, const Math::Vector2<int>& position) {
            switch(key) {
                case Key::Up:
                    o->rotate(PI/18, -1, 0, 0);
                    break;
                case Key::Down:
                    o->rotate(PI/18, 1, 0, 0);
                    break;
                case Key::Left:
                    o->rotate(PI/18, 0, -1, 0, false);
                    break;
                case Key::Right:
                    o->rotate(PI/18, 0, 1, 0, false);
                    break;
                case Key::PageUp:
                    camera->translate(0, 0, -0.5);
                    break;
                case Key::PageDown:
                    camera->translate(0, 0, 0.5);
                    break;
                case Key::Home:
                    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_FILL : GL_LINE);
                    wireframe = !wireframe;
                    break;
                case Key::End:
                    if(fps) cout << "Average FPS on " << camera->viewport().x()
                        << 'x' << camera->viewport().y() << " from "
                        << totalmeasurecount << " measures: "
                        << totalfps/totalmeasurecount << "          " << endl;
                    else before = chrono::high_resolution_clock::now();

                    fps = !fps;
                    frames = totalmeasurecount = 0;
                    totalfps = 0;
                    break;
            }

            redraw();
        }

        void mouseEvent(MouseButton button, MouseState state, const Math::Vector2<int>& position) {
            switch(button) {
                case MouseButton::Left:
                    if(state == MouseState::Down) previousPosition = positionOnSphere(position);
                    else previousPosition = Vector3();
                    break;
                case MouseButton::WheelUp:
                case MouseButton::WheelDown: {
                    if(state == MouseState::Up) return;

                    /* Distance between origin and near camera clipping plane */
                    GLfloat distance = camera->transformation().at(3).z()-0-camera->near();

                    /* Move 15% of the distance back or forward */
                    if(button == MouseButton::WheelUp)
                        distance *= 1 - 1/0.85f;
                    else
                        distance *= 1 - 0.85f;
                    camera->translate(0, 0, distance);

                    redraw();
                    break;
                }
                default: ;
            }
        }

        void mouseMoveEvent(const Math::Vector2<int>& position) {
            Vector3 currentPosition = positionOnSphere(position);

            Vector3 axis = Vector3::cross(previousPosition, currentPosition);

            if(previousPosition.length() < 0.001f || axis.length() < 0.001f) return;

            GLfloat angle = acos(previousPosition*currentPosition);
            o->rotate(angle, axis);

            previousPosition = currentPosition;

            redraw();
        }

    private:
        Vector3 positionOnSphere(const Math::Vector2<int>& _position) const {
            Math::Vector2<GLsizei> viewport = camera->viewport();
            Vector2 position(_position.x()*2.0f/viewport.x() - 1.0f,
                             _position.y()*2.0f/viewport.y() - 1.0f);

            GLfloat length = position.length();
            Vector3 result(length > 1.0f ? position : Vector3(position, 1.0f - length));
            result.setY(-result.y());
            return result.normalized();
        }

        Scene scene;
        Camera* camera;
        PhongShader shader;
        IndexedMesh mesh;
        Object* o;
        chrono::high_resolution_clock::time_point before;
        bool wireframe, fps;
        size_t frames;
        double totalfps;
        size_t totalmeasurecount;
        Vector3 previousPosition;
};

}}

MAGNUM_EXAMPLE_MAIN(Magnum::Examples::ViewerExample)
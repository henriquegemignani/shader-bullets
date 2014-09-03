#include <ugdk/system/engine.h>
#include <ugdk/action/scene.h>
#include <ugdk/input/events.h>

#include <ugdk/desktop/module.h>
#include <ugdk/desktop/window.h>
#include <ugdk/graphic/module.h>
#include <ugdk/graphic/rendertarget.h>
#include <ugdk/graphic/canvas.h>
#include <ugdk/graphic/vertexdata.h>
#include <ugdk/graphic/opengl/vertexbuffer.h>
#include <ugdk/graphic/opengl/shader.h>
#include <ugdk/graphic/opengl/shaderprogram.h>
#include <ugdk/util/idgenerator.h>

#include <random>
#include <cmath>
#include <list>

#define M_PI 3.1415926535897932384626433832795

using namespace ugdk;
using namespace ugdk::graphic::opengl;

namespace {
    ShaderProgram* shaderprog = nullptr;
    void InitOurShader() {
        shaderprog = new ShaderProgram;

        Shader vertex_shader(GL_VERTEX_SHADER), geometry_shader(GL_GEOMETRY_SHADER), fragment_shader(GL_FRAGMENT_SHADER);

        vertex_shader.set_source(R"(#version 150
in vec2 vertexPosition;
in float radius;
in vec3 color;

out VertexData {
    float radius;
    vec3 color;
} VertexOut;

uniform mat4 geometry_matrix;
void main() {
    VertexOut.radius = radius;
    VertexOut.color = color;
    gl_Position = geometry_matrix * vec4(vertexPosition,0,1);
}
)");

        geometry_shader.set_source(R"(#version 150
layout(points) in;
layout(triangle_strip, max_vertices=4) out;
uniform mat4 geometry_matrix;
uniform vec2 CANVAS_SIZE;

in VertexData {
    float radius;
    vec3 color;
} VertexIn[];

out VertexData {
    vec2 dir;
    vec3 color;
} VertexOut;

#define M_PI 3.1415926535897932384626433832795

void EmitVertexAtAngle(float angle) {
    vec2 dir = vec2(cos(angle), sin(angle));
    gl_Position = gl_in[0].gl_Position + geometry_matrix * vec4(VertexIn[0].radius * dir, 0.0, 0.0);
    VertexOut.dir = dir;
    VertexOut.color = VertexIn[0].color;
    EmitVertex();
}

void main()
{	
    if(VertexIn[0].radius > 0.5) {
        EmitVertexAtAngle(3.0 * M_PI / 4.0);
        EmitVertexAtAngle(1.0 * M_PI / 4.0);
        EmitVertexAtAngle(5.0 * M_PI / 4.0);
        EmitVertexAtAngle(7.0 * M_PI / 4.0);
    }
    EndPrimitive();
}
)");
        // FRAGMENT
        fragment_shader.set_source(R"(#version 150

uniform vec2 CANVAS_SIZE;

in VertexData {
    vec2 dir;
    vec3 color;
} VertexIn;

void main() {
    float dist = 1.5 * length(VertexIn.dir);
        float alpha = 1 - pow(dist, 3);
        gl_FragColor = vec4(VertexIn.color, alpha);
}
)");                              

        shaderprog->AttachShader(vertex_shader);
        shaderprog->AttachShader(geometry_shader);
        shaderprog->AttachShader(fragment_shader);

        glBindAttribLocation(shaderprog->id(), 1, "radius");
        glBindAttribLocation(shaderprog->id(), 2, "color");

        bool status = shaderprog->SetupProgram();
        assert(status);
    }

    const int kMaxNumBullets = 128;
    struct BulletGraphic {
        float x, y;
        float radius;
        float r, g, b;
    };

    struct BulletLogic {
        math::Vector2D speed;
        double time_left;
        int id;
    };
}

int main(int argc, char* argv[]) {
    ugdk::system::Initialize();
    desktop::manager()->primary_window()->ChangeSettings(math::Integer2D(800, 600), false, false);

    // Seed with a real random value, if available
    std::random_device rd;

    auto screen_size = graphic::manager()->screen()->size();

    std::default_random_engine e1(rd());
    std::uniform_int_distribution<int> border_dist(0, 4);
    std::uniform_real_distribution<float> width_dist(screen_size.x * 0.2f, screen_size.x * 0.8f);
    std::uniform_real_distribution<float> height_dist(screen_size.y * 0.2f, screen_size.y * 0.8f);
    std::uniform_real_distribution<float> radius_dist(10.0f, 30.0f);
    std::uniform_real_distribution<float> color_dist(0.5f, 1.0f);
    std::uniform_real_distribution<float> random_speed(80.0f, 140.0f);
    std::uniform_real_distribution<float> random_angle(-M_PI / 2, M_PI / 2);

    InitOurShader();

    {
        ugdk::action::Scene* ourscene = new ugdk::action::Scene;
        graphic::VertexData data(kMaxNumBullets, sizeof(BulletGraphic), true, true);
        BulletGraphic* bullets = static_cast<BulletGraphic*>(data.buffer()->map());
        util::IDGenerator bulletGenerator(0, kMaxNumBullets - 1, -1);

        std::list<BulletLogic> bulletLogics;

        for (int i = 0; i < kMaxNumBullets; ++i) {
            bullets[i].radius = 0.0f;
        }

        /*{
            bullets[i].x = width_dist(e1);
            bullets[i].y = height_dist(e1);
            bullets[i].radius = radius_dist(e1);
            bullets[i].r = color_dist(e1);
            bullets[i].g = color_dist(e1);
            bullets[i].b = color_dist(e1);
        }*/

        const double kNewBulletThreshold = 0.25;
        double new_bullet_timer = 0.0;

        ourscene->AddTask([&](double dt) {
            for (auto& logic : bulletLogics) {
                logic.time_left -= dt;
                bullets[logic.id].x += logic.speed.x * dt;
                bullets[logic.id].y += logic.speed.y * dt;
                if (logic.time_left < 0.0) {
                    bullets[logic.id].radius = 0.0f;
                    bulletGenerator.ReleaseID(logic.id);
                }
            }

            bulletLogics.remove_if([](const BulletLogic& logic) { return logic.time_left < 0.0; });

            new_bullet_timer += dt;
            if (new_bullet_timer > kNewBulletThreshold) {
                new_bullet_timer -= kNewBulletThreshold;
                int new_bullet_id = bulletGenerator.GenerateID();
                if (new_bullet_id != bulletGenerator.error_value()) {
                    math::Vector2D speed(random_speed(e1), 0.0);
                    int border = border_dist(e1);
                    double angle = random_angle(e1) * 0.75;
                    if (border < 3) {
                        bullets[new_bullet_id].x = (border % 2 == 1) * screen_size.x;
                        bullets[new_bullet_id].y = height_dist(e1);
                        angle += M_PI * (border % 2 == 1);
                    } else {
                        angle += M_PI / 2;
                        bullets[new_bullet_id].x = width_dist(e1);
                        bullets[new_bullet_id].y = (border % 2 == 1) * screen_size.y;
                        angle += M_PI * (border % 2 == 1);
                    }
                    BulletLogic logic = {
                        speed.Rotate(angle),
                        15.0,
                        new_bullet_id,
                    };
                    bullets[new_bullet_id].radius = radius_dist(e1);
                    bullets[new_bullet_id].r = color_dist(e1);
                    bullets[new_bullet_id].g = color_dist(e1);
                    bullets[new_bullet_id].b = color_dist(e1);
                    bulletLogics.push_back(logic);
                }
            }

        });

        ourscene->set_render_function([&](graphic::Canvas& canvas) {
            canvas.ChangeShaderProgram(shaderprog);
            canvas.SendUniform("CANVAS_SIZE", canvas.size());

            canvas.SendVertexData(data, graphic::VertexType::VERTEX, 0, 2);
            canvas.SendVertexData(data, graphic::VertexType::TEXTURE, sizeof(float) * 2, 1);
            canvas.SendVertexData(data, graphic::VertexType::COLOR, sizeof(float) * 3, 3);
            shaderprog->Validate();

            canvas.DrawArrays(graphic::DrawMode::POINTS(), 0, kMaxNumBullets);
        });

        ourscene->event_handler().AddListener<ugdk::input::KeyPressedEvent>(
            [ourscene](const ugdk::input::KeyPressedEvent& ev) {
            if (ev.scancode == ugdk::input::Scancode::ESCAPE)
                ourscene->Finish();
        });

        ugdk::system::PushScene(ourscene);
        ugdk::system::Run();
    }    
    ugdk::system::Release();
    return 0;
}

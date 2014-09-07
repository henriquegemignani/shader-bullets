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
in vec4 vertexPosition; // origin_pos.x, origin_pos.y, start_time, type
in vec4 dataA;
in vec4 dataB;
in vec4 dataC;

vec2 origin_pos;
uniform float current_time;

out VertexData {
    float radius;
    vec3 color;
} VertexOut;

uniform mat4 geometry_matrix;
void outputPosition(vec2 position) {
    gl_Position = geometry_matrix * vec4(position,0,1);
}

void Polynomial(float t) {
    VertexOut.radius = dataA.w;
    VertexOut.color = dataA.xyz;
 
    vec4 tExp = vec4(t, t*t, t*t*t, t*t*t*t);
    vec4 vX = dataB * tExp;
    vec4 vY = dataC * tExp;
    outputPosition(origin_pos + vec2(vX.x + vX.y + vY.z + vY.w, vY.x + vY.y + vY.z + vY.w));
}

void Spiral(float t) {
    VertexOut.radius = dataA.w;
    VertexOut.color = dataA.xyz;

    vec4 tExp = vec4(t, t*t, t*t*t, t*t*t*t);
    vec4 vR = dataB * tExp;

    float r = vR.x + vR.y + vR.z + vR.w;
    float ang = dataC.x + t * dataC.y + t*t * dataC.z + t*t*t * dataC.w;
 
    outputPosition(origin_pos + r * vec2(cos(ang), sin(ang)));
}

void CubicBezier(float time) {
    VertexOut.radius = dataA.w;
    VertexOut.color = dataA.xyz;

    vec2 p0 = origin_pos;
    vec2 p1 = dataB.xy;
    vec2 p2 = dataB.zw;
    vec2 p3 = dataC.xy;
    float lifetime = dataC.z;

    float t = time/lifetime;
    outputPosition(pow(1 - t, 3) * p0 + 3*pow(1 - t, 2)*t* p1 + 3 * (1 - t) * t*t * p2 + t*t*t * p3);
}

void Invalid() {
    VertexOut.radius = 0.0;
    outputPosition(vec2(0, 0));
}

void main() {
    origin_pos = vertexPosition.xy;
    float start_time = vertexPosition.z;
    int type = int(vertexPosition.w);

    float t = current_time - start_time;

    switch(type) {
        case 1:
            Polynomial(t);
            break;
        case 2:
            Spiral(t);
            break;
        case 3:
            CubicBezier(t);
            break;
        default:
            Invalid();
    }
}
)");

        geometry_shader.set_source(R"(#version 150
layout(points) in;
layout(triangle_strip, max_vertices=4) out;
uniform mat4 geometry_matrix;

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

    const int kMaxNumBullets = 1024;
    struct BulletGraphic {
        struct {
            glm::vec2 origin_pos;
            float start_time;
            float type;
        } vertexPosition;
        glm::vec4 dataA;
        glm::vec4 dataB;
        glm::vec4 dataC;
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
    glEnable(GL_BLEND);
    double current_time = 0.0;

    {
        ugdk::action::Scene* ourscene = new ugdk::action::Scene;
        graphic::VertexData data(kMaxNumBullets, sizeof(BulletGraphic), true, true);
        BulletGraphic* bullets = static_cast<BulletGraphic*>(data.buffer()->map());
        util::IDGenerator bulletGenerator(0, kMaxNumBullets - 1, -1);

        std::list<BulletLogic> bulletLogics;

        for (int i = 0; i < kMaxNumBullets; ++i) {
            bullets[i].vertexPosition.type = 0.0f;
        }

        /*for (int i = 0; i < 16; ++i) {
            bullets[i].vertexPosition.origin_pos = glm::vec2(screen_size.x / 2.0, screen_size.y / 2.0);
            bullets[i].vertexPosition.start_time = current_time;
            bullets[i].vertexPosition.type = 2.0f;

            bullets[i].dataA = glm::vec4(1.0, 1.0f, 0.0f, 50.0f);
            bullets[i].dataB = glm::vec4(20.0, 10.0f, 0.0f, 0.0f);
            bullets[i].dataC = glm::vec4((i/16.0) * 2 * M_PI, 1.0f, 0.0f, 0.0f);
        }*/

        {
            int i = 17;
            bullets[i].vertexPosition.origin_pos = glm::vec2(0.0f, 0.0f); //screen_size.x / 2.0, screen_size.y / 2.0);
            bullets[i].vertexPosition.start_time = current_time;
            bullets[i].vertexPosition.type = 3.0f;

            bullets[i].dataA = glm::vec4(1.0, 1.0f, 0.0f, 50.0f);
            bullets[i].dataB = glm::vec4(screen_size.x, 0.0f, 0.0f, screen_size.y);
            bullets[i].dataC = glm::vec4(screen_size.x, screen_size.y, 20.0f, 0.0f);
        }

        const double kNewBulletThreshold = 0.001;
        double new_bullet_timer = 0.0;

        ourscene->AddTask([&](double dt) {
            current_time += dt;
        });

        ourscene->set_render_function([&](graphic::Canvas& canvas) {
            canvas.ChangeShaderProgram(shaderprog);
            canvas.SendUniform("current_time", static_cast<float>(current_time));

            canvas.SendVertexData(data, graphic::VertexType::VERTEX,  offsetof(BulletGraphic, vertexPosition), 4);
            canvas.SendVertexData(data, graphic::VertexType::TEXTURE, offsetof(BulletGraphic, dataA), 4);
            canvas.SendVertexData(data, graphic::VertexType::COLOR,   offsetof(BulletGraphic, dataB), 4);
            canvas.SendVertexData(data, graphic::VertexType::CUSTOM1, offsetof(BulletGraphic, dataC), 4);
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

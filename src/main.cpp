#include <iostream>
#include <array>
#include <string>
#include <string_view>

#include <glad/glad.h>
struct FullscreenQuad {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint program = 0;

    void create() {
        constexpr std::array<float, 18> vertices = {
            -1.f, -1.f, 0.f,
             1.f, -1.f, 0.f,
             1.f,  1.f, 0.f,
            -1.f, -1.f, 0.f,
             1.f,  1.f, 0.f,
            -1.f,  1.f, 0.f,
        };

        constexpr std::string_view vSource = R"(
            #version 460 core
            layout(location = 0) in vec3 position;
            layout(location = 0) out vec2 uv;
            void main() {
                gl_Position = vec4(position, 1.0);
                uv = vec2(position.xy * 0.5 + 0.5);
            }
        )";
        constexpr std::string_view fSource = R"(
            #version 460 core

            layout(location = 0) in vec2 uv;
            layout(location = 0) out vec4 color;

            layout(std140, binding = 0) uniform Frame {
                mat4 inverseViewProjection;
                vec4 cameraPosition;
                vec4 sunDirection;
                vec4 sunIrradiance;
            };

            // Bruneton 2008, converted to 1/m and m. Planar geometry, not the paper's spherical LUT.
            const vec3  kRayleighScattering = vec3(5.8e-6, 13.5e-6, 33.1e-6);
            const float kMieScattering = 4.0e-6;
            const float kMieExtinction = kMieScattering / 0.9;
            const float kRayleighScaleHeight = 8000.0;
            const float kMieScaleHeight = 1200.0;
            const float kAtmosphereTop = 60000.0;
            const float kMieG = 0.76;
            const int   kViewSteps = 64;
            const float kPi = 3.14159265358979323846;
            const float kExposure = 1.0;

            float rayleighPhase(float mu) {
                return 3.0 / (16.0 * kPi) * (1.0 + mu * mu);
            }

            float miePhase(float mu, float g) {
                float g2 = g * g;
                return 3.0 / (8.0 * kPi) * (1.0 - g2) * (1.0 + mu * mu)
                    / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
            }

            // ∫ exp(-(y0 + t*dy)/H) dt between y0 and y1, dy != 0
            float heightIntegral(float y0, float y1, float dy, float H) {
                return (H / dy) * (exp(-y0 / H) - exp(-y1 / H));
            }

            vec3 transmittance(vec3 p0, vec3 p1, vec3 dir) {
                float dy = dir.y;
                vec3 tauR = kRayleighScattering * heightIntegral(p0.y, p1.y, dy, kRayleighScaleHeight);
                vec3 tauM = vec3(kMieExtinction) * heightIntegral(p0.y, p1.y, dy, kMieScaleHeight);
                return exp(-(tauR + tauM));
            }

            vec3 sunTransmittance(vec3 p, vec3 sunDir) {
                if (sunDir.y <= 0.0) {
                    return vec3(0.0);
                }
                float tTop = (kAtmosphereTop - p.y) / sunDir.y;
                return transmittance(p, p + sunDir * tTop, sunDir);
            }

            vec3 scatterInscattered(vec3 origin, vec3 dir, float tEnd, float mu) {
                float pR = rayleighPhase(mu);
                float pM = miePhase(mu, kMieG);
                float dt = tEnd / float(kViewSteps);
                vec3 inscattered = vec3(0.0);
                for (int i = 0; i < kViewSteps; ++i) {
                    float t = (float(i) + 0.5) * dt;
                    vec3 p = origin + dir * t;
                    float rhoR = exp(-p.y / kRayleighScaleHeight);
                    float rhoM = exp(-p.y / kMieScaleHeight);
                    vec3 scatter = kRayleighScattering * rhoR * pR + vec3(kMieScattering) * rhoM * pM;
                    inscattered += transmittance(origin, p, dir) * scatter * sunTransmittance(p, sunDirection.xyz) * dt;
                }
                return inscattered;
            }

            void main() {
                vec2 ndc = uv * 2.0 - 1.0;
                vec4 worldFar = inverseViewProjection * vec4(ndc, 1.0, 1.0);
                vec3 origin = cameraPosition.xyz;
                vec3 dir = normalize(worldFar.xyz / worldFar.w - origin);
                float mu = dot(dir, sunDirection.xyz);

                vec3 inscattered;
                if (dir.y > 0.0) {
                    float tEnd = (kAtmosphereTop - origin.y) / dir.y;
                    inscattered = scatterInscattered(origin, dir, tEnd, mu);
                } else if (dir.y < 0.0) {
                    float tEnd = -origin.y / dir.y;
                    inscattered = scatterInscattered(origin, dir, tEnd, mu);
                } else {
                    float rhoR = exp(-origin.y / kRayleighScaleHeight);
                    float rhoM = exp(-origin.y / kMieScaleHeight);
                    vec3 ext = kRayleighScattering * rhoR + vec3(kMieExtinction) * rhoM;
                    vec3 scat = kRayleighScattering * rhoR * rayleighPhase(mu)
                        + vec3(kMieScattering) * rhoM * miePhase(mu, kMieG);
                    inscattered = (scat / ext) * sunTransmittance(origin, sunDirection.xyz);
                }

                vec3 radiance = sunIrradiance.rgb * inscattered;
                vec3 ldr = vec3(1.0) - exp(-radiance * kExposure);
                color = vec4(pow(ldr, vec3(1.0 / 2.2)), 1.0);
            }
        )";

        const auto compile = [](GLenum type, std::string_view src) {
            const GLuint shader = glCreateShader(type);
            const GLchar* data = src.data();
            const GLint length = static_cast<GLint>(src.size());
            glShaderSource(shader, 1, &data, &length);
            glCompileShader(shader);
            GLint ok = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (ok != GL_TRUE) {
                throw std::runtime_error("shader compile failed");
            }
            return shader;
        };

        const GLuint vs = compile(GL_VERTEX_SHADER, vSource);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, fSource);
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            throw std::runtime_error("program link failed");
        }
        glDetachShader(program, vs);
        glDetachShader(program, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        glCreateBuffers(1, &vbo);
        glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(sizeof(vertices)), vertices.data(), 0);

        glCreateVertexArrays(1, &vao);
        glVertexArrayVertexBuffer(vao, 0, vbo, 0, 3 * sizeof(float));
        glEnableVertexArrayAttrib(vao, 0);
        glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(vao, 0, 0);
    }

    void draw() const {
        glUseProgram(program);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
};
FullscreenQuad quad;

#include <cmath>
#include <stdexcept>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
struct Camera {
    glm::vec3 position{ 0.f };
    glm::mat4 view{ 1.f };
    glm::mat4 projection{ 1.f };
    glm::mat4 viewProjection{ 1.f };
    glm::mat4 inverseViewProjection{ 1.f };

    void setView(float eyeHeight, float pitch) {
        if (!(eyeHeight > 0.f)) {
            throw std::runtime_error("Camera::setView: eyeHeight must be > 0");
        }

        constexpr float halfPi = 1.5707963267948966f;
        if (!(pitch > 0.f && pitch < halfPi)) {
            throw std::runtime_error("Camera::setView: pitch must be in (0, pi/2)");
        }

        position = glm::vec3{ 0.f, eyeHeight, 0.f };
        const glm::vec3 direction{ 0.f, std::sin(pitch), std::cos(pitch) };
        view = glm::lookAt(position, position + direction, glm::vec3{ 0.f, 1.f, 0.f });

        compose();
    }

    void setProjection(float fovY, float aspect, float nearZ, float farZ) {
        if (!(fovY > 0.f && fovY < 3.1415926535897932f)) {
            throw std::runtime_error("Camera::setProjection: fovY must be in (0, pi)");
        }

        if (!(aspect > 0.f)) {
            throw std::runtime_error("Camera::setProjection: aspect must be > 0");
        }

        if (!(nearZ > 0.f && farZ > nearZ)) {
            throw std::runtime_error("Camera::setProjection: require farZ > nearZ > 0");
        }

        projection = glm::perspective(fovY, aspect, nearZ, farZ);
        compose();
    }

private:
    void compose() {
        viewProjection = projection * view;
        inverseViewProjection = glm::inverse(viewProjection);
    }
};
Camera camera;

#include <GLFW/glfw3.h>
int width = 1280;
int height = 720;
int lastWidth = width;
int lastHeight = height;

float eyeHeight = 1.8f;
float pitch = glm::radians(15.f);
float fovY = glm::radians(60.f);
float nearZ = 0.1f;
float farZ = 50000.f;

struct GPUFrame {
    glm::mat4 inverseViewProjection;
    glm::vec4 cameraPosition;
    glm::vec4 sunDirection;
    glm::vec4 sunIrradiance;
};
static_assert(sizeof(GPUFrame) == 112);

GLuint frameUbo = 0;
glm::vec3 sunDirection = glm::normalize(glm::vec3{ 0.f, 1.f, 1.f });
glm::vec3 sunIrradiance{ 20.f, 20.f, 20.f };

static void uploadFrame() {
    const GPUFrame frame{
        camera.inverseViewProjection,
        glm::vec4{camera.position, 1.f},
        glm::vec4{sunDirection, 0.f},
        glm::vec4{sunIrradiance, 0.f},
    };
    glNamedBufferSubData(frameUbo, 0, sizeof(frame), &frame);
}

#include "core/core.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(width, height, "Weather System", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // 垂直同步

    gladLoadGL();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");

    quad.create();

    glCreateBuffers(1, &frameUbo);
    glNamedBufferStorage(frameUbo, sizeof(GPUFrame), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUbo);

    camera.setView(eyeHeight, pitch);
    camera.setProjection(fovY, static_cast<float>(width) / static_cast<float>(height), nearZ, farZ);
    
    uploadFrame();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        if (width != lastWidth || height != lastHeight) {
            lastWidth = width;
            lastHeight = height;

            camera.setProjection(fovY, static_cast<float>(width) / static_cast<float>(height), nearZ, farZ);
            uploadFrame();
        }

        glClearColor(.0f, .0f, .0f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

#pragma region memory usage
        auto usage = sample_usage();
        char text[128];
        std::snprintf(text, sizeof(text), "fps %.1f\ncpu %.0f%%\ngpu %.0f%%",
            ImGui::GetIO().Framerate, usage.cpu, usage.gpu);
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        draw_list->AddText(ImVec2(9.f, 9.f), IM_COL32(0, 0, 0, 180), text);
        draw_list->AddText(ImVec2(8.f, 8.f), IM_COL32(255, 255, 255, 255), text);
#pragma endregion

        quad.draw();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
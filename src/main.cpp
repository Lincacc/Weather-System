#include "core/core.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

FullscreenQuad quad;
GLuint weatherSysProgram = 0;

#include "core/camera.h"
float eyeHeight = 1.8f;
float pitch = glm::radians(45.0f);
float fovY = glm::radians(60.f);
float nearZ = 0.1f;
float farZ = 50000.f;
std::uint32_t t = 0;
Camera camera;

struct GPUFrame {
    glm::mat4 inverseViewProjection;
    glm::vec4 camPosition;
    glm::vec4 sunDirection;
    glm::vec4 sunIrradiance;
    glm::uvec4 status;
};
static_assert(sizeof(GPUFrame) == 128);

#include <GLFW/glfw3.h>
int width = 1280;
int height = 720;
int lastWidth = width;
int lastHeight = height;

GLuint frameUbo = 0;

float sunElevation = glm::radians(45.f);
float sunAzimuth = 0.f;
float sunIntensity = 20.f;

static void uploadFrame() {
    auto sunDirFromAngles = [](float elevation, float azimuth) {
        const float ce = std::cos(elevation);
        return glm::vec3{
            ce * std::sin(azimuth),
            std::sin(elevation),
            ce * std::cos(azimuth),
        };
    }; 
    const bool sunGone = sunElevation < glm::radians(0.);
    const float bodyElevation = sunGone ? -sunElevation : sunElevation;
    const glm::vec3 sunDirection = sunDirFromAngles(bodyElevation, sunAzimuth);
    const glm::vec3 sunIrradiance{ sunIntensity, sunIntensity, sunIntensity };

    const GPUFrame frame{
        camera.inverseViewProjection,
        glm::vec4{ camera.position, 1.f },
        glm::vec4{sunDirection, 0.f},
        glm::vec4{sunIrradiance, 0.f},
        glm::uvec4{
            t,
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            sunGone ? 1u : 0u
        }
    };

    glNamedBufferSubData(frameUbo, 0, sizeof(GPUFrame), &frame);
}

static void drawFullscreen(GLuint program) {
    glUseProgram(program);
    glBindVertexArray(quad.vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

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
    weatherSysProgram = compileProgram(vSource, fSource);

    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("framebuffer size must be > 0");
    }
    lastWidth = width;
    lastHeight = height;

    camera.setView(eyeHeight, pitch);
    camera.setProjection(fovY, static_cast<float>(width) / static_cast<float>(height), nearZ, farZ);

    glCreateBuffers(1, &frameUbo);
    glNamedBufferStorage(frameUbo, sizeof(GPUFrame), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUbo);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0) {
            lastWidth = width;
            lastHeight = height;

            t = 0;

            continue;
        }

        if (width != lastWidth || height != lastHeight) {
            lastWidth = width;
            lastHeight = height;

            t = 0;

            camera.setProjection(fovY, static_cast<float>(width) / static_cast<float>(height), nearZ, farZ);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

#pragma region operate panel
        float elevDeg = glm::degrees(sunElevation);
        float azimDeg = glm::degrees(sunAzimuth);
        const ImVec2 display = ImGui::GetIO().DisplaySize;

        ImGui::SetNextWindowPos(ImVec2(display.x - 8.f, 8.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(260.f, 0.f), ImGuiCond_Always);
        ImGui::Begin("Atmosphere", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        constexpr float labelW = 110.f;
        auto slider = [&](const char* label, const char* id, float* v, float vmin, float vmax) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(labelW);
            ImGui::SetNextItemWidth(-1.f);
            ImGui::SliderFloat(id, v, vmin, vmax);
            };

        slider("Elevation", "##sun_elev", &elevDeg, -90.f, 90.f);
        slider("Azimuth", "##sun_azim", &azimDeg, -180.f, 180.f);
        slider("Irradiance", "##sun_irr", &sunIntensity, 0.f, 40.f);

        ImGui::End();

        sunElevation = glm::radians(elevDeg);
        sunAzimuth = glm::radians(azimDeg);
#pragma endregion

#pragma region memory usage
        auto usage = host_usage();
        char text[128];
        std::snprintf(text, sizeof(text), "fps %.1f\ncpu %.0f%%\ngpu %.0f%%",
            ImGui::GetIO().Framerate, usage.cpu, usage.gpu);
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        draw_list->AddText(ImVec2(9.f, 9.f), IM_COL32(0, 0, 0, 180), text);
        draw_list->AddText(ImVec2(8.f, 8.f), IM_COL32(255, 255, 255, 255), text);
#pragma endregion

        glViewport(0, 0, width, height);

        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        uploadFrame();
        drawFullscreen(weatherSysProgram);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        t += 1u;

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
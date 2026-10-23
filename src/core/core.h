#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <cstdio>
#include <vector>
#include <array>

struct Usage {
    float cpu = .0f;
    float gpu = .0f;
};

static Usage host_usage() {
    static Usage usage;
    static LARGE_INTEGER freq{};
    static LARGE_INTEGER last_qpc{};
    static FILETIME last_idle{};
    static FILETIME last_kernel{};
    static FILETIME last_user{};
    static PDH_HQUERY query = nullptr;
    static PDH_HCOUNTER counter = nullptr;
    static bool ready = false;
    auto to_u64 = [](const FILETIME& ft) {
        return (ULONGLONG(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
    if (!ready) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last_qpc);
        GetSystemTimes(&last_idle, &last_kernel, &last_user);
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS &&
            PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) == ERROR_SUCCESS) {
            PdhCollectQueryData(query);
        }
        ready = true;
        return usage;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    double elapsed = double(now.QuadPart - last_qpc.QuadPart) / double(freq.QuadPart);
    if (elapsed < 0.5)
        return usage;
    last_qpc = now;
    FILETIME idle{}, kernel{}, user{};
    GetSystemTimes(&idle, &kernel, &user);
    ULONGLONG d_idle = to_u64(idle) - to_u64(last_idle);
    ULONGLONG d_total = (to_u64(kernel) - to_u64(last_kernel)) + (to_u64(user) - to_u64(last_user));
    last_idle = idle;
    last_kernel = kernel;
    last_user = user;
    if (d_total > 0)
        usage.cpu = float(d_total - d_idle) * 100.f / float(d_total);
    if (query && PdhCollectQueryData(query) == ERROR_SUCCESS) {
        DWORD size = 0;
        DWORD count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr) == PDH_MORE_DATA) {
            std::vector<unsigned char> buffer(size);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS) {
                double gpu = 0.0;
                for (DWORD i = 0; i < count; ++i) {
                    if (wcsstr(items[i].szName, L"engtype_3D") == nullptr)
                        continue;
                    if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
                        continue;
                    if (items[i].FmtValue.doubleValue > gpu)
                        gpu = items[i].FmtValue.doubleValue;
                }
                usage.gpu = static_cast<float>(gpu);
            }
        }
    }
    return usage;
}

#include "shader.h"
struct FullscreenQuad {
    GLuint vao = 0;
    GLuint vbo = 0;

    void create() {
        constexpr std::array<float, 18> vertices = {
            -1.f, -1.f, 0.f,
             1.f, -1.f, 0.f,
             1.f,  1.f, 0.f,
            -1.f, -1.f, 0.f,
             1.f,  1.f, 0.f,
            -1.f,  1.f, 0.f,
        };

        glCreateBuffers(1, &vbo);
        glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(sizeof(vertices)), vertices.data(), 0);

        glCreateVertexArrays(1, &vao);
        glVertexArrayVertexBuffer(vao, 0, vbo, 0, 3 * sizeof(float));
        glEnableVertexArrayAttrib(vao, 0);
        glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(vao, 0, 0);
    }
};

#include <iostream>
static GLuint compileShader(GLenum type, std::string_view src) {
    const GLuint shader = glCreateShader(type);
    const GLchar* data = src.data();
    const GLint length = static_cast<GLint>(src.size());

    glShaderSource(shader, 1, &data, &length);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        std::string log(static_cast<std::size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        log.resize(static_cast<std::size_t>(length) - 1);
        std::cerr << log << '\n';

        throw std::runtime_error("shader compile failed");
    }
    return shader;
}

static GLuint compileProgram(std::string_view vSource, std::string_view fSource) {
    const GLuint vs = compileShader(GL_VERTEX_SHADER, vSource);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fSource);
    const GLuint program = glCreateProgram();

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

    return program;
}

static GLuint compileCompute(std::string_view source) {
    const GLuint cs = compileShader(GL_COMPUTE_SHADER, source);
    const GLuint program = glCreateProgram();

    glAttachShader(program, cs);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        throw std::runtime_error("program link failed");
    }

    glDetachShader(program, cs);
    glDeleteShader(cs);

    return program;
}
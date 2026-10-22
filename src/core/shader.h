#pragma once

#include <glad/glad.h>

#include <string_view>
#include <stdexcept>

using namespace std::string_view_literals;

static GLuint compileProgram(std::string_view vSource, std::string_view fSource) {
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
    const float kSunAngularRadius = 0.01935 / 2.0;
    const float kExposure = 1.0;

    float rayleighPhase(float mu) {
        return 3.0 / (16.0 * kPi) * (1.0 + mu * mu);
    }

    float miePhase(float mu, float g) {
        float g2 = g * g;
        return 3.0 / (8.0 * kPi) * (1.0 - g2) * (1.0 + mu * mu)
            / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
    }

    // กา exp(-(y0 + t*dy)/H) dt between y0 and y1, dy != 0
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
        if (dir.y > 0.0 && mu >= cos(kSunAngularRadius)) {
            float tTop = (kAtmosphereTop - origin.y) / dir.y;
            vec3 T = transmittance(origin, origin + dir * tTop, dir);
            vec3 Lsun = sunIrradiance.rgb / (kPi * kSunAngularRadius * kSunAngularRadius);
            radiance += Lsun * T;
        }

        vec3 ldr = vec3(1.0) - exp(-radiance * kExposure);
        color = vec4(pow(ldr, vec3(1.0 / 2.2)), 1.0);
    }
)"sv;
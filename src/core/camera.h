#pragma once

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
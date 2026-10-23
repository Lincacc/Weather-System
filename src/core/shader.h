#pragma once

#include <glad/glad.h>

#include <string_view>
#include <stdexcept>

using namespace std::string_view_literals;

constexpr std::string_view vSource = R"(
    #version 460 core

    layout(location = 0) in vec3 position;
    layout(location = 0) out vec2 ndc;

    void main() {
        ndc = position.xy * 2.0 - 1.0;
        gl_Position = vec4(position, 1.0);
    }
)";

constexpr std::string_view fSource = R"(
    #version 460 core

    layout(location = 0) in vec2 ndc;
    layout(location = 0) out vec4 color;

    layout(std140, binding = 0) uniform GPUFrame {
        mat4 inverseViewProjection;
        vec4 cameraPosition;
        vec4 sunDirection;
        vec4 sunIrradiance;
        uvec4 status;
    };

// Bruneton 2008, converted to 1/m and m. Planar geometry, not the paper's spherical LUT.
const vec3  kRayleighScattering = vec3(5.8e-6, 13.5e-6, 33.1e-6);
const float kMieScattering = 4.0e-6;
const float kMieExtinction = kMieScattering / 0.9;
const float kRayleighScaleHeight = 8000.0;
const float kMieScaleHeight = 1200.0;
const float kPlanetRadius = 6361e3;
const float kAtmosphereRadius = 6369e3;
const vec3  kPlanetCenter = vec3(0.0, -kPlanetRadius, 0.0);
const float kMieG = 0.76;
const int   kViewSteps = 64;
const float kPi = 3.14159265358979323846;

    float escape(vec3 p, vec3 d, float R) {
        vec3 v = p - kPlanetCenter;
        float b = dot(v, d);
        float cdot = dot(v, v) - R * R;
        float det2 = b * b - cdot;
        if (det2 < 0.0) {
            return -1.0;
        }
        float det = sqrt(det2);
        float t1 = -b - det;
        float t2 = -b + det;
        return (t1 >= 0.0) ? t1 : t2;
    }

    float rayleighPhase(float mu) {
        return 3.0 / (16.0 * kPi) * (1.0 + mu * mu);
    }

    float miePhase(float mu, float g) {
        float g2 = g * g;
        return 3.0 / (8.0 * kPi) * (1.0 - g2) * (1.0 + mu * mu)
            / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
    }

    // 沿球面累积光学厚度
    vec3 scatterInscattered(vec3 origin, vec3 dir, float mu) {
        float tEnd = escape(origin, dir, kAtmosphereRadius);
        float pR = rayleighPhase(mu);
        float pM = miePhase(mu, kMieG);
        float dt = tEnd / float(kViewSteps);
        vec3 inscattered = vec3(0.0);
        vec3 tauR = vec3(0.0);
        float tauM = 0.0;
        for (int i = 0; i < kViewSteps; ++i) {
            float t = (float(i) + 0.5) * dt;
            vec3 p = origin + dir * t;
            float h = length(p - kPlanetCenter) - kPlanetRadius;
            float rhoR = exp(-h / kRayleighScaleHeight);
            float rhoM = exp(-h / kMieScaleHeight);
            tauR += kRayleighScattering * rhoR * dt;
            tauM += kMieExtinction * rhoM * dt;

            float pS = miePhase(mu, 0.999);
            vec3 scatter = kRayleighScattering * rhoR * pR
                + vec3(kMieScattering) * rhoM * (pM + 10.0 * pS);

            float tSun = escape(p, sunDirection.xyz, kAtmosphereRadius);
            float dtSun = tSun / 4.0;
            vec3 tauSun = vec3(0.0);
            for (int j = 0; j < 4; ++j) {
                vec3 ps = p + sunDirection.xyz * (float(j) * dtSun);
                float hs = length(ps - kPlanetCenter) - kPlanetRadius;
                tauSun += (kRayleighScattering * exp(-hs / kRayleighScaleHeight)
                    + vec3(kMieExtinction) * exp(-hs / kMieScaleHeight)) * dtSun;
            }
            inscattered += exp(-(tauR + vec3(tauM))) * scatter * exp(-tauSun) * dt;
        }
        return inscattered;
    }

#define SEA_NUM_STEPS 7.
#define SEA_ITER_FRAGMENT 5
#define SEA_HEIGHT 0.6
#define SEA_CHOPPY 4.0
#define SEA_SPEED 0.8
#define SEA_FREQ 0.16
#define SEA_BASE vec3(0.1,0.19,0.22)
#define SEA_ITER_GEOMETRY 3
#define SEA_TIME (1.0 + float(status.x) * 0.01 * SEA_SPEED)
#define SEA_EPSILON_NRM	(0.1 / float(status.y))
#define SEA_WATER_COLOR vec3(0.8,0.9,0.6)

    float hash( vec2 p ) {
        float h = dot(p,vec2(127.1,311.7));	
        return fract(sin(h)*43758.5453123);
    }

    float sea_noise( in vec2 p ) {
        vec2 i = floor( p );
        vec2 f = fract( p );	
        vec2 u = f*f*(3.0-2.0*f);

        return -1.0+2.0*mix( 
            mix( hash( i + vec2(0.0,0.0) ), 
                hash( i + vec2(1.0,0.0) ), 
                u.x),
            mix( hash( i + vec2(0.0,1.0) ), 
                hash( i + vec2(1.0,1.0) ), 
                u.x), 
            u.y);
    }

    float sea_octave( in vec2 uv, const in float choppy) {
        uv += sea_noise(uv);
        vec2 wv = 1.0-abs(sin(uv));
        vec2 swv = abs(cos(uv));
        wv = mix(wv,swv,wv);
        return pow(1.0-pow(wv.x * wv.y,0.65),choppy);
    }

    //海平面和 p 的高度差
    float sea_height_diff(vec3 p) {
        float freq = SEA_FREQ;
        float amp = SEA_HEIGHT;
        float choppy = SEA_CHOPPY;
        vec2 uv = p.xz; uv.x *= 0.75;

        float d, h = 0.0;    
        for(int i = 0; i < SEA_ITER_GEOMETRY; i++) {
            d = sea_octave((uv+SEA_TIME)*freq,choppy);
            d += sea_octave((uv-SEA_TIME)*freq,choppy);
            h += d * amp;
            uv *= mat2(1.6,1.2,-1.2,1.6); freq *= 1.9; amp *= 0.22;
            choppy = mix(choppy,1.0,0.2);
        }
        return p.y - h;
    }

    float sea_height_diff2(vec3 p) {
        float freq = SEA_FREQ;
        float amp = SEA_HEIGHT;
        float choppy = SEA_CHOPPY;
        vec2 uv = p.xz; uv.x *= 0.75;

        float d, h = 0.0;    
        for(int i = 0; i < SEA_ITER_FRAGMENT; i++) {
            d = sea_octave((uv+SEA_TIME)*freq,choppy);
            d += sea_octave((uv-SEA_TIME)*freq,choppy);
            h += d * amp;
            uv *= mat2(1.6,1.2,-1.2,1.6); freq *= 1.9; amp *= 0.22;
            choppy = mix(choppy,1.0,0.2);
        }
        return p.y - h;
    }

    vec3 sea_normal(vec3 p, float eps) {
        vec3 n;
        n.y = sea_height_diff2(p);    
        n.x = sea_height_diff2(vec3(p.x+eps,p.y,p.z)) - n.y;
        n.z = sea_height_diff2(vec3(p.x,p.y,p.z+eps)) - n.y;
        n.y = eps;
        return normalize(n);
    }

    // 求交海面，击中后返回交点
    float traceSea(vec3 ori, vec3 dir) {
        float tmin = 0.0;
        float tmax = 50000.0;   
        float hmax = sea_height_diff(ori + dir * tmax);
        if(hmax > 0.0) return tmax;
        float hmin = sea_height_diff(ori + dir * tmin);
        
        float tmid = 0.0;
        for(int i = 0; i < int(SEA_NUM_STEPS); i++) {
            tmid = mix(tmin,tmax, hmin/(hmin-hmax));                   
            vec3 p = ori + dir * tmid;                   
            float hmid = sea_height_diff(p);
            if(hmid < 0.0) {
                tmax = tmid;
                hmax = hmid;
            } else {
                tmin = tmid;
                hmin = hmid;
            }
        }

        return tmid;
    }

    vec3 getSeaColor(vec3 p, vec3 n, vec3 l, vec3 eye, vec3 dist, vec3 skyColor) {
        float fresnel = clamp(1.0 - dot(n,-eye), 0.0, 1.0);
        fresnel = pow(fresnel,3.0) * 0.5;

        float diff = pow(dot(n,l) * 0.4 + 0.6, 80.0);
        vec3 refracted = SEA_BASE + diff * SEA_WATER_COLOR * 0.12; 
        
        vec3 color = mix(refracted,skyColor,fresnel) * skyColor;

        float atten = max(1.0 - dot(dist,dist) * 0.001, 0.0);
        color +=  clamp(SEA_WATER_COLOR * (p.y - SEA_HEIGHT) * 0.08 * atten,0.,1.);

        float spec_fract = (40.0 + 8.0) / (3.1415 * 8.0);
        float spec = pow(max(dot(reflect(eye,n),l),0.0), 40.0) * spec_fract;
        color += clamp(vec3(spec)* skyColor * 0.5,0.,1.);

        color = pow(color,vec3(0.65));

        return color;
    }

    void main() {
        vec4 worldFar = inverseViewProjection * vec4(ndc, 1.0, 1.0);
        vec3 ori = cameraPosition.xyz;
        vec3 dir = normalize(worldFar.xyz / worldFar.w - ori);
        vec3 sun_dir = normalize(sunDirection.xyz);

    vec3 radiance;
    if (dir.y > 0.0) {
        vec3 sky_linear = sunIrradiance.rgb * scatterInscattered(ori, dir, dot(dir, sun_dir));
        radiance = pow(sky_linear, vec3(1.0 / 2.2));
    } else {
        float L = -ori.y / dir.y;
        vec3 atmo_p = ori + dir * L;
        vec3 atmo_dir = dir;
        atmo_dir.y = -atmo_dir.y;
        vec3 sky_linear = sunIrradiance.rgb * scatterInscattered(atmo_p, atmo_dir, dot(atmo_dir, sun_dir));
        vec3 sky_color = pow(sky_linear, vec3(1.0 / 2.2));

        vec3 sea_p = ori + dir * traceSea(ori, dir);
        vec3 sea_dist = sea_p - ori;
        vec3 sea_n = sea_normal(sea_p, dot(sea_dist, sea_dist) * SEA_EPSILON_NRM);
        vec3 sea_color = getSeaColor(sea_p, sea_n, sun_dir, dir, sea_dist, sky_color);
        radiance = mix(sky_color, sea_color, pow(smoothstep(0.0, -0.05, dir.y), 0.5));
    }

        color = vec4(radiance, 1.0);
    }
)"sv;
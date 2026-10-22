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
        uvec4 status;
    };

#define SEA_NUM_STEPS 7.
#define SEA_ITER_FRAGMENT 5
#define SEA_HEIGHT 0.6
#define SEA_CHOPPY 4.0
#define SEA_SPEED 0.8
#define SEA_FREQ 0.16
#define SEA_BASE vec3(0.1,0.19,0.22)
#define SEA_ITER_GEOMETRY 3
#define SEA_TIME (1.0 + status.x * 0.005 * SEA_SPEED)
#define SEA_EPSILON_NRM	(0.1 / status.x)
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

    vec3 getSkyColor(vec3 e) {
        e.y = (max(e.y,0.0)*0.8+0.2)*0.8;
        return vec3(pow(1.0-e.y,2.0), 1.0-e.y, 0.6+(1.0-e.y)*0.4) * 1.1;
    }

    vec3 getSeaColor(vec3 p, vec3 n, vec3 l, vec3 eye, vec3 dist, vec3 skyColor) {
        float RAINBOW_WATER_SATURATION = 0.35;
        float RAINBOW_WATER_LIGHTNESS = 0.1; //0.2
        float RAINBOW_WATER_SPEED = 0.1;
        float fresnel = clamp(1.0 - dot(n,-eye), 0.0, 1.0);
        fresnel = pow(fresnel,3.0) * 0.5;

        vec3 reflected = getSkyColor(reflect(eye,n));

        vec3 weights = vec3(0.2125, 0.7154, 0.0721); 
        float luminance = dot(reflected, weights);
        reflected = mix(vec3(luminance), reflected, 0.1);
        float diff = pow(dot(n,l) * 0.4 + 0.6, 80.0);
        vec3 refracted = SEA_BASE + diff * SEA_WATER_COLOR * 0.12; 

        vec3 color = mix(refracted,skyColor,fresnel) * skyColor;
        float atten = max(1.0 - dot(dist,dist) * 0.001, 0.0);
        color +=  clamp(SEA_WATER_COLOR * (p.y - SEA_HEIGHT) * 0.08 * atten,0.,1.);

        float spec_fract = (60.0 + 8.0) / (3.1415 * 8.0);
        float spec = pow(max(dot(reflect(eye,n),l),0.0),60.0) * spec_fract;
        color += clamp(vec3(spec)* skyColor * 0.5,0.,1.);

        color = pow(color,vec3(0.65));

        return color;
    }

    void main() {
        vec4 worldFar = inverseViewProjection * vec4(ndc, 1.0, 1.0);
        vec3 ori = cameraPosition.xyz;
        vec3 dir = normalize(worldFar.xyz / worldFar.w - ori);
        vec3 sun_dir = normalize(sunDirection.xyz);
        vec3 sky_color = getSkyColor(dir);

        vec3 sea_p = ori + dir * traceSea(ori, dir);
        vec3 sea_dist = sea_p - ori;
        vec3 sea_n = sea_normal(sea_p, dot(sea_dist, sea_dist) * SEA_EPSILON_NRM);
        vec3 sea_c = getSeaColor(sea_p,sea_n,sun_dir,dir,sea_dist,sky_color);

        color = vec4(sea_c, 1.0);
    }
)"sv;
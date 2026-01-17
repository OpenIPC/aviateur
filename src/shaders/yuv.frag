#version 310 es

#ifdef GL_ES
precision highp float;
precision highp sampler2D;
#endif

#ifdef VULKAN
layout(location=0) out vec4 oFragColor;
layout(location=0) in vec2 v_texCoord;

layout(binding = 1) uniform sampler2D tex_y;
layout(binding = 2) uniform sampler2D tex_u;
layout(binding = 3) uniform sampler2D tex_v;
#else
out vec4 oFragColor;
in vec2 v_texCoord;

uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;
#endif

#ifdef VULKAN
layout(binding = 0) uniform bUniform0 {
#else
layout(std140) uniform bUniform0 {
#endif
    mat4 xform;
    int pixFmt;
    int pad0;
    int pad1;
    int pad2;
};

void main() {
    vec3 yuv;
    vec3 rgb;
    if (pixFmt == 0 || pixFmt == 12) {
        //yuv420p
        yuv.x = texture(tex_y, v_texCoord).r;
        yuv.y = texture(tex_u, v_texCoord).r - 0.5;
        yuv.z = texture(tex_v, v_texCoord).r - 0.5;
        rgb = mat3(1.0, 1.0, 1.0,
        0.0, -0.3455, 1.779,
        1.4075, -0.7169, 0.0) * yuv;
    } else if (pixFmt == 23){
        // NV12
        yuv.x = texture(tex_y, v_texCoord).r;
        yuv.y = texture(tex_u, v_texCoord).r - 0.5;
        yuv.z = texture(tex_u, v_texCoord).g - 0.5;
        rgb = mat3(
        1.0, 1.0, 1.0,
        0.0, -0.3455, 1.779,
        1.4075, -0.7169, 0.0) * yuv;
    } else {
        //YUV444P
        yuv.x = texture(tex_y, v_texCoord).r;
        yuv.y = texture(tex_u, v_texCoord).r - 0.5;
        yuv.z = texture(tex_v, v_texCoord).r - 0.5;

        rgb.x = clamp(yuv.x + 1.402 *yuv.z, 0.0, 1.0);
        rgb.y = clamp(yuv.x - 0.34414 * yuv.y - 0.71414 * yuv.z, 0.0, 1.0);
        rgb.z = clamp(yuv.x + 1.772 * yuv.y, 0.0, 1.0);
    }

    oFragColor = vec4(rgb, 1.0);
}

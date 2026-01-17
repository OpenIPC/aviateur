#version 310 es

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

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

#ifdef VULKAN
layout(location=0) out vec2 v_texCoord;
#else
out vec2 v_texCoord;
#endif

void main() {
    gl_Position = xform * vec4(aPos, 1.0f, 1.0f);
    v_texCoord = aUV;
}

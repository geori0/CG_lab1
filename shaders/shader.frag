#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 tintColor;
} ubo;

void main() {
    vec3 baseColor = fragColor * ubo.tintColor.rgb;
    outColor = vec4(baseColor, ubo.tintColor.a);
}
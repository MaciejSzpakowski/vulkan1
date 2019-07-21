#version 450
#extension GL_ARB_separate_shader_objects : enable
// https://www.khronos.org/opengl/wiki/Layout_Qualifier_(GLSL)

layout(binding = 0) uniform UniformBufferObject {
    float scale;
    float x;
	float y;
} ubo;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = vec4(inPosition.x * ubo.scale + ubo.x, inPosition.y * ubo.scale + ubo.y, 0.0, 1.0);
    fragColor = inColor;
	fragTexCoord = inTexCoord;
}


attribute vec3 a_position;
attribute vec3 a_normal;
attribute vec2 a_texCoord;

varying vec3 v_normal;
varying vec2 v_texCoord;
varying vec3 v_fragPos;

uniform mat4 u_modelMat;
uniform mat4 u_viewMat;
uniform mat4 u_projMat;
uniform mat4 u_normalMat;

void main() {
    vec4 worldPos = u_modelMat * vec4(a_position, 1.0);
    v_fragPos = worldPos.xyz;
    v_normal = normalize((u_normalMat * vec4(a_normal, 0.0)).xyz);
    v_texCoord = a_texCoord;
    gl_Position = u_projMat * u_viewMat * worldPos;
}

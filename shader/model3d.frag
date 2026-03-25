
#ifdef GLSLES
precision mediump float;
#endif

varying vec3 v_normal;
varying vec2 v_texCoord;
varying vec3 v_fragPos;

uniform sampler2D u_diffuseTex;
uniform bool u_hasDiffuseTex;
uniform vec4 u_diffuseColor;
uniform vec3 u_lightDir;
uniform float u_ambient;

void main() {
    vec4 texColor;
    if (u_hasDiffuseTex) {
        texColor = texture2D(u_diffuseTex, v_texCoord) * u_diffuseColor;
    } else {
        texColor = u_diffuseColor;
    }

    vec3 norm = normalize(v_normal);
    vec3 lightDir = normalize(u_lightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 result = (u_ambient + diff) * texColor.rgb;

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), texColor.a);
}

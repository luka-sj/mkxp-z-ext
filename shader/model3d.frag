
#ifdef GLSLES
precision mediump float;
#endif

varying vec3 v_normal;
varying vec2 v_texCoord;
varying vec3 v_fragPos;

uniform sampler2D u_diffuseTex;
uniform vec4 u_diffuseColor;
uniform vec3 u_lightDir;
uniform float u_ambient;

void main() {
    vec4 tex = texture2D(u_diffuseTex, v_texCoord);

    /* Cutout texels must not write depth, or they punch holes through the
     * geometry drawn after them. Tested before the material multiply so a
     * translucent `d` still renders. */
    if (tex.a < 0.02)
        discard;

    vec4 texColor = tex * u_diffuseColor;

    /* Two-sided: meshes are full of single-sided faces, and a face whose
     * normal points away should still be lit rather than flat ambient. */
    vec3 norm = normalize(v_normal);
    vec3 lightDir = normalize(u_lightDir);
    float lambert = abs(dot(norm, lightDir));

    /* Ambient is a floor, not a summand: the light only ever darkens the
     * texture, so diffuse colours stay true instead of clamping to white. */
    float shade = u_ambient + (1.0 - u_ambient) * lambert;

    gl_FragColor = vec4(texColor.rgb * shade, texColor.a);
}

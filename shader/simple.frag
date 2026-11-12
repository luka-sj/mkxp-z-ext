
uniform sampler2D texture;
uniform mediump float depthCut;

varying vec2 v_texCoord;

void main()
{
	gl_FragColor = texture2D(texture, v_texCoord);
	if (gl_FragColor.a < depthCut)
		discard;
}

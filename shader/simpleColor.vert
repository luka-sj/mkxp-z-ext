
uniform mat4 projMat;

uniform vec2 texSizeInv;
uniform vec2 translation;
uniform vec2 viewportScale;

attribute vec2 position;
attribute vec2 texCoord;
attribute lowp vec4 color;

varying vec2 v_texCoord;
varying lowp vec4 v_color;

void main()
{
	gl_Position = projMat * vec4(position * viewportScale + translation, 0, 1);

	v_texCoord = texCoord * texSizeInv;
	v_color = color;
}

varying vec2 v_texCoord;

uniform sampler2D texture;

void main()
{
	vec4 col = texture2D(texture,v_texCoord);

    gl_FragColor = col;
}

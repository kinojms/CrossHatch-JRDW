$input v_color

#include <bgfx_shader.sh>

void main()
{
	// Output vertex color as-is; BGFX provides RGBA for a_color when Color0 is ABGR-packed
	gl_FragColor = v_color;
}
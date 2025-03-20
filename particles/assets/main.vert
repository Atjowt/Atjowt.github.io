precision mediump float;

attribute vec4 aPos;

varying vec2 p;

void main(void) {
	gl_Position = vec4(aPos.xy, 0.0, 1.0);
	p = aPos.zw * 2.0 - 1.0;
}

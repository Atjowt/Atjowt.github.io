precision mediump float;

varying vec2 p;

void main(void) {
	if (dot(p, p) > 1.0) discard;
	gl_FragColor = vec4(p, 1.0, 1.0);
}

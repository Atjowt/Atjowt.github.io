#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>

#define NUM_PARTICLES 1024
#define INV_RADIUS 64
#define PARTICLE_RADIUS (1.0f / INV_RADIUS)
#define TIMESTEP 0.002f
#define MOUSE_FORCE 16.0f
#define GRAVITY 4.0f
#define RESTITUTION 0.6f
#define DIST_EPSILON 0.000001f

#define GRID_WIDTH INV_RADIUS
#define GRID_HEIGHT INV_RADIUS
#define CELL_CAP 8
#define CELL_WIDTH (2.0f / GRID_WIDTH)
#define CELL_HEIGHT (2.0f / GRID_HEIGHT)

#define RANDOM() (rand() / (float)RAND_MAX)
#define MAX_INFO_LOG 512

float viewport[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
float mouse[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

struct {
	float (*curr)[2];
	float (*prev)[2];
	int* index;
} particle;

struct {
	int keys[CELL_CAP];
	int count;
}** grid;

void cellAppend(int key, int x, int y) {
	if (grid[y][x].count >= CELL_CAP) {
		fprintf(stdout, "Overfull cell!\n");
		return;
	}
	grid[y][x].keys[grid[y][x].count++] = key;
}

void collideParticles(int i, int j) {
	float x1 = particle.curr[i][0];
	float y1 = particle.curr[i][1];
	float px1 = particle.prev[i][0];
	float py1 = particle.prev[i][1];
	float vx1 = x1 - px1;
	float vy1 = y1 - py1;
	float x2 = particle.curr[j][0];
	float y2 = particle.curr[j][1];
	float px2 = particle.prev[j][0];
	float py2 = particle.prev[j][1];
	float vx2 = x2 - px2;
	float vy2 = y2 - py2;
	float dx = x1 - x2;
	float dy = y1 - y2;
	float rsum = PARTICLE_RADIUS + PARTICLE_RADIUS;
	float rsum2 = rsum * rsum;
	float dist2 = dx * dx + dy * dy;
	if (dist2 <= rsum2) {
		float dist = sqrtf(dist2);
		float nx, ny;
		if (dist >= DIST_EPSILON) {
			nx = dx / dist;
			ny = dy / dist;
		} else {
			nx = 0.0f;
			ny = 1.0f;
			printf("Division by near-zero value! (%f)\n", dist);
		}

		float overlap = rsum - dist;
		float factor1 = 0.5f;
		float factor2 = 0.5f;

		particle.curr[i][0] += factor1 * overlap * nx;
		particle.curr[i][1] += factor1 * overlap * ny;
		particle.curr[j][0] -= factor2 * overlap * nx;
		particle.curr[j][1] -= factor2 * overlap * ny;

		float vrelx = vx1 - vx2;
		float vrely = vy1 - vy2;
		float vreln = vrelx * nx + vrely * ny;

		if (vreln < 0.0f) {
			float impulse = -(1.0f + RESTITUTION) * vreln * 0.5f;
			vx1 += impulse * nx;
			vy1 += impulse * ny;
			vx2 -= impulse * nx;
			vy2 -= impulse * ny;
			particle.prev[i][0] = particle.curr[i][0] - vx1;
			particle.prev[i][1] = particle.curr[i][1] - vy1;
			particle.prev[j][0] = particle.curr[j][0] - vx2;
			particle.prev[j][1] = particle.curr[j][1] - vy2;
		}
	}
}

void gridcollision(void) {
	int x0 = 1;
	int x1 = GRID_WIDTH - 2;
	int y0 = 1;
	int y1 = GRID_HEIGHT - 2;
	int mx = x0 + (x1 - x0) / 2;
	int my = y0 + (y1 - y0) / 2;
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			int keys[9 * CELL_CAP];
			int count = 0;
			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					for (int ci = 0; ci < grid[y+dy][x+dx].count; ci++) {
						keys[count++] = grid[y+dy][x+dx].keys[ci];
					}
				}
			}
			for (int i = 0; i < count - 1; i++) {
				int key1 = keys[i];
				for (int j = i + 1; j < count; j++) {
					int key2 = keys[j];
					collideParticles(key1, key2);
				}
			}
		}
	}
}

void compileShaderSource(GLsizei n, GLchar const* const* sources, GLint const* lengths, GLuint* shader) {
	glShaderSource(*shader, n, sources, lengths);
	glCompileShader(*shader);
	GLint compiled;
	glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);
	if (compiled != GL_TRUE) {
		GLchar info[MAX_INFO_LOG];
		GLsizei len;
		glGetShaderInfoLog(*shader, sizeof(info), &len, info);
		fprintf(stdout, "Failed to compile shader:\n%.*s\n", len, info);
		exit(1);
	}
}

void compileShaderFiles(GLsizei n, char const* const* filenames, GLuint* shader) {
	GLchar** sources = malloc(n * sizeof(GLchar*));
	GLint* lengths = malloc(n * sizeof(GLint));
	for (size_t i = 0; i < n; i++) {
		const char* filename = filenames[i];
		printf("Sourcing file '%s'...\n", filename);
		FILE* file = fopen(filename, "r");
		if (!file) {
			fprintf(stdout, "Failed open '%s'\n", filename);
			exit(1);
		}
		fseek(file, 0, SEEK_END);
		long filesize = ftell(file);
		GLchar* source = malloc(filesize * sizeof(GLchar));
		fseek(file, 0, SEEK_SET);
		size_t length = fread(source, sizeof(GLchar), filesize, file);
		fclose(file);
		sources[i] = source;
		lengths[i] = length;
	}
	printf("Compiling shader...\n");
	compileShaderSource(n, (const GLchar* const*)sources, lengths, shader);
	printf("OK!\n");
	for (size_t i = 0; i < n; i++) {
		free(sources[i]);
	}
	free(sources);
	free(lengths);
}

void linkShaderProgram(GLuint* program) {
	glLinkProgram(*program);
	GLint linked;
	glGetProgramiv(*program, GL_LINK_STATUS, &linked);
	if (linked != GL_TRUE) {
		GLchar info[MAX_INFO_LOG];
		GLsizei len;
		glGetProgramInfoLog(*program, sizeof(info), &len, info);
		fprintf(stdout, "Failed to link program:\n%.*s\n", len, info);
		exit(1);
	}
}

void allocParticles(void) {
	particle.curr = malloc(NUM_PARTICLES * sizeof(float[2]));
	particle.prev = malloc(NUM_PARTICLES * sizeof(float[2]));
	particle.index = malloc(NUM_PARTICLES * sizeof(int));
}

void allocGrid(void) {
	grid = malloc(GRID_HEIGHT * sizeof(*grid));
	for (int y = 0; y < GRID_HEIGHT; y++) {
		grid[y] = malloc(GRID_WIDTH * sizeof(**grid));
	}
}

void freeParticles(void) {
	free(particle.curr);
	free(particle.prev);
	free(particle.index);
}

void freeGrid(void) {
	for (int y = 0; y < GRID_HEIGHT; y++) {
		free(grid[y]);
	}
	free(grid);
}

float timePrev;
float secTimer;
int fpsCounter;
float timestepTimer;

GLuint vertexBuffer;
GLuint shaderProgram;

GLfloat vertices[NUM_PARTICLES][6][4]; 

void genvertices(void) {
	for (int i = 0; i < NUM_PARTICLES; i++) {

		float x = particle.curr[i][0];
		float y = particle.curr[i][1];

		vertices[i][0][0] = x - PARTICLE_RADIUS;
		vertices[i][0][1] = y - PARTICLE_RADIUS;
		vertices[i][0][2] = 0.0f;
		vertices[i][0][3] = 0.0f;

		vertices[i][1][0] = x - PARTICLE_RADIUS;
		vertices[i][1][1] = y + PARTICLE_RADIUS;
		vertices[i][1][2] = 0.0f;
		vertices[i][1][3] = 1.0f;

		vertices[i][2][0] = x + PARTICLE_RADIUS;
		vertices[i][2][1] = y + PARTICLE_RADIUS;
		vertices[i][2][2] = 1.0f;
		vertices[i][2][3] = 1.0f;

		vertices[i][3][0] = x + PARTICLE_RADIUS;
		vertices[i][3][1] = y + PARTICLE_RADIUS;
		vertices[i][3][2] = 1.0f;
		vertices[i][3][3] = 1.0f;

		vertices[i][4][0] = x + PARTICLE_RADIUS;
		vertices[i][4][1] = y - PARTICLE_RADIUS;
		vertices[i][4][2] = 1.0f;
		vertices[i][4][3] = 0.0f;

		vertices[i][5][0] = x - PARTICLE_RADIUS;
		vertices[i][5][1] = y - PARTICLE_RADIUS;
		vertices[i][5][2] = 0.0f;
		vertices[i][5][3] = 0.0f;
	}
}

void mainloop(void) {

	float timeCurr = emscripten_get_now();
	float deltaTime = (timeCurr - timePrev) / 1000.0f;
	timePrev = timeCurr;

	if (secTimer >= 1.0f) {
		printf("%d FPS\n", fpsCounter);
		fpsCounter = 0;
		secTimer -= 1.0f;
	}
	secTimer += deltaTime;
	fpsCounter++;

	timestepTimer += deltaTime;

	while (timestepTimer >= TIMESTEP) {
		timestepTimer -= TIMESTEP;
		float dt = TIMESTEP;

		// Move particles with verlet integration
		for (int i = 0; i < NUM_PARTICLES; i++) {
			float x = particle.curr[i][0];
			float y = particle.curr[i][1];
			float px = particle.prev[i][0];
			float py = particle.prev[i][1];
			float dx = x - px;
			float dy = y - py;
			float ax = 0.0f;
			float ay = 0.0f;
			ax += mouse[2] * MOUSE_FORCE * (mouse[0] - x);
			ax -= mouse[3] * MOUSE_FORCE * (mouse[0] - x);
			ay += mouse[2] * MOUSE_FORCE * (mouse[1] - y);
			ay -= mouse[3] * MOUSE_FORCE * (mouse[1] - y);
			ay -= GRAVITY;
			particle.prev[i][0] = x;
			particle.prev[i][1] = y;
			float damp = 0.9995f;
			particle.curr[i][0] = x + damp * dx + ax*dt*dt;
			particle.curr[i][1] = y + damp * dy + ay*dt*dt;
		}

		// Clear all previous grid data
		for (int y = 0; y < GRID_HEIGHT; y++) {
			for (int x = 0; x < GRID_WIDTH; x++) {
				grid[y][x].count = 0;
			}
		}

		// Populate grid data with particles
		for (int i = 0; i < NUM_PARTICLES; i++) {
			float px = particle.curr[i][0];
			float py = particle.curr[i][1];
			int x = (px + 1.0f) * 0.5f * GRID_WIDTH;
			int y = (py + 1.0f) * 0.5f * GRID_HEIGHT;
			x = x < 0 ? 0 : x;
			x = x >= GRID_WIDTH ? GRID_WIDTH - 1 : x;
			y = y < 0 ? 0 : y;
			y = y >= GRID_HEIGHT ? GRID_HEIGHT - 1 : y;
			cellAppend(i, x, y);
		}

		gridcollision();

		// Apply maximum distance constraint
		for (int i = 0; i < NUM_PARTICLES; i++) {
			float x = particle.curr[i][0];
			float y = particle.curr[i][1];
			float dist2 = x * x + y * y;
			float dist = sqrtf(dist2);
			float maxDist = 1.0f - PARTICLE_RADIUS;
			float minDist = PARTICLE_RADIUS + 0.3f;
			float nx = x / dist;
			float ny = y / dist;
			dist = fmaxf(fminf(dist, maxDist), minDist);
			particle.curr[i][0] = nx * dist;
			particle.curr[i][1] = ny * dist;
		}
	}

	// Send particle positions to GPU
	genvertices();
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

	// Make draw call
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 6 * NUM_PARTICLES);

}

EM_BOOL on_mousemove(int eventType, const EmscriptenMouseEvent* e, void* userData) {
	float x = e->targetX;
	float y = e->targetY;
	mouse[0] = +(2.0f * (x - viewport[2]) / viewport[0] - 1.0f);
	mouse[1] = -(2.0f * (y - viewport[3]) / viewport[1] - 1.0f);
	return EM_TRUE;
}

EM_BOOL on_mousedown(int eventType, const EmscriptenMouseEvent* e, void* userData) {
	if (e->button == 0) {
		mouse[2] = 1;
	} else if (e->button == 2) {
		mouse[3] = 1;
	}
	return EM_TRUE;
}

EM_BOOL on_mouseup(int eventType, const EmscriptenMouseEvent* e, void* userData) {
	if (e->button == 0) {
		mouse[2] = 0;
	} else if (e->button == 2) {
		mouse[3] = 0;
	}
	return EM_TRUE;
}

int main(void) {

	srand(time(NULL));

	EmscriptenWebGLContextAttributes attrs;
	emscripten_webgl_init_context_attributes(&attrs);
	attrs.alpha = EM_FALSE;
	attrs.depth = EM_FALSE;
	attrs.stencil = EM_FALSE;
	attrs.antialias = EM_TRUE;

	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context("#canvas", &attrs);
	if (context < 0) {
		printf("Failed to create WebGL context!\n");
		return -1;
	}
	emscripten_webgl_make_context_current(context);
	// emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, on_resize);
	emscripten_set_mousemove_callback("#canvas", NULL, EM_TRUE, on_mousemove);
	emscripten_set_mousedown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, on_mousedown);
	emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, on_mouseup);

	int initWidth = 1024, initHeight = 1024;
	viewport[0] = initWidth;
	viewport[1] = initHeight;
	viewport[2] = 0;
	viewport[3] = 0;
	emscripten_set_canvas_element_size("#canvas", initWidth, initHeight);
	glViewport(0, 0, initWidth, initHeight);

	printf("OpenGL %s\n", glGetString(GL_VERSION));

	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	compileShaderFiles(1, (const char* []) { "assets/main.vert" }, &vertexShader);

	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	compileShaderFiles(1, (const char* []) { "assets/main.frag" }, &fragmentShader);

	shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	linkShaderProgram(&shaderProgram);
	glUseProgram(shaderProgram);

	glGenBuffers(1, &vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), NULL, GL_DYNAMIC_DRAW);

	GLint posAttrib = glGetAttribLocation(shaderProgram, "aPos");
	glEnableVertexAttribArray(posAttrib);
	glVertexAttribPointer(posAttrib, 4, GL_FLOAT, GL_FALSE, 0, (void*)0);

	timePrev = 0.0f;
	secTimer = 0.0f;
	fpsCounter = 0;

	timestepTimer = 0.0f;

	allocParticles();
	allocGrid();

	// init random particles
	for (int i = 0; i < NUM_PARTICLES; i++) {
		float x = 2.0f * RANDOM() - 1.0f;
		float y = 2.0f * RANDOM() - 1.0f;
		float dx = 0.0f;
		float dy = 0.0f;
		particle.curr[i][0] = x;
		particle.curr[i][1] = y;
		particle.prev[i][0] = x - dx;
		particle.prev[i][1] = y - dy;
		particle.index[i] = i;
	}

	emscripten_set_main_loop(mainloop, 0, 1);

	return 0;
}

// EMSCRIPTEN_KEEPALIVE
// EM_BOOL on_resize(int eventType, const EmscriptenUiEvent* e, void* userData) {
// 	int width = e->windowInnerWidth;
// 	int height = e->windowInnerHeight;
// 	int viewportWidth;
// 	int viewportHeight;
// 	int viewportX;
// 	int viewportY;
// 	if (width == height) {
// 		viewportWidth = width;
// 		viewportHeight = height;
// 		viewportX = 0;
// 		viewportY = 0;
// 	}
// 	if (width < height) {
// 		viewportWidth = width;
// 		viewportHeight = width;
// 		viewportX = 0;
// 		viewportY = (height - viewportHeight) / 2;
// 	}
// 	if (width > height) {
// 		viewportWidth = height;
// 		viewportHeight = height;
// 		viewportX = (width - viewportWidth) / 2;
// 		viewportY = 0;
// 	}
// 	viewport[0] = viewportWidth;
// 	viewport[1] = viewportHeight;
// 	viewport[2] = viewportX;
// 	viewport[3] = viewportY;
// 	glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
// 	return EM_TRUE;
// }
//

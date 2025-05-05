#include <windows.h>
//#include <src/gl.h>
//#include "src/glad.c"
#include <math.h>
#include <stdio.h>
#include <stdlib.h> // For malloc/free
#include <string.h> // For memset

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <glad/glad.h>
#include "GLFW/glfw3.h"

// Shadow map resolution
const unsigned int SHADOW_WIDTH = 1024, SHADOW_HEIGHT = 1024;
GLuint depthMapFBO;
GLuint depthMap;
GLuint depthShaderProgram;

// Simple BMP loader for 24-bit uncompressed BMP
unsigned char* loadBMP(const char* filename, int* width, int* height) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    unsigned char header[54];
    fread(header, 1, 54, f);
    if (header[0] != 'B' || header[1] != 'M') { fclose(f); return NULL; }
    *width = *(int*)&header[18];
    *height = *(int*)&header[22];
    int bpp = *(short*)&header[28];
    if (bpp != 24) { fclose(f); return NULL; }
    int row_padded = (*width * 3 + 3) & (~3);
    unsigned char* data = (unsigned char*)malloc(row_padded * (*height));
    if (!data) { fclose(f); return NULL; }
    fread(data, 1, row_padded * (*height), f);
    fclose(f);
    // BMP is BGR and upside down, convert to RGB and flip
    unsigned char* rgb = (unsigned char*)malloc(3 * (*width) * (*height));
    for (int y = 0; y < *height; ++y) {
        for (int x = 0; x < *width; ++x) {
            int bmp_idx = (y * row_padded) + x * 3;
            int rgb_idx = ((*height - 1 - y) * (*width) + x) * 3;
            rgb[rgb_idx + 0] = data[bmp_idx + 2];
            rgb[rgb_idx + 1] = data[bmp_idx + 1];
            rgb[rgb_idx + 2] = data[bmp_idx + 0];
        }
    }
    free(data);
    return rgb;
}

GLuint g_tex = 0;

// Callback to adjust viewport on window resize
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// Camera orbit variables
float cam_yaw = 0.0f;    // left-right
float cam_pitch = 30.0f; // up-down (degrees)
float cam_dist = 12.0f;  // distance from center
int mouse_down = 0;
double last_mouse_x = 0, last_mouse_y = 0;

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            mouse_down = 1;
            glfwGetCursorPos(window, &last_mouse_x, &last_mouse_y);
        } else if (action == GLFW_RELEASE) {
            mouse_down = 0;
        }
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mouse_down) {
        double dx = xpos - last_mouse_x;
        double dy = ypos - last_mouse_y;
        cam_yaw   += (float)dx * 0.4f;
        cam_pitch += (float)dy * 0.4f;
        if (cam_pitch > 89.0f) cam_pitch = 89.0f;
        if (cam_pitch < -89.0f) cam_pitch = -89.0f;
        last_mouse_x = xpos;
        last_mouse_y = ypos;
    }
}

// Shader loading utility
char* load_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);
    return buf;
}

GLuint compile_shader(const char* path, GLenum type) {
    char* src = load_file(path);
    if (!src) { printf("Failed to load %s\n", path); exit(1); }
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, (const char**)&src, NULL);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(shader, 512, NULL, info);
        printf("Shader compile error (%s): %s\n", path, info);
        exit(1);
    }
    free(src);
    return shader;
}

GLuint create_program(const char* vs_path, const char* fs_path) {
    GLuint vs = compile_shader(vs_path, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fs_path, GL_FRAGMENT_SHADER);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(prog, 512, NULL, info);
        printf("Program link error: %s\n", info);
        exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// Cube vertex data (positions, normals, texcoords)
float cube_vertices[] = {
    // positions        // normals         // texcoords
    // Front face
    -0.5f,-0.5f, 0.5f,  0,0,1,  0,0,
     0.5f,-0.5f, 0.5f,  0,0,1,  1,0,
     0.5f, 0.5f, 0.5f,  0,0,1,  1,1,
    -0.5f, 0.5f, 0.5f,  0,0,1,  0,1,
    // Back face
    -0.5f,-0.5f,-0.5f,  0,0,-1, 1,0,
    -0.5f, 0.5f,-0.5f,  0,0,-1, 1,1,
     0.5f, 0.5f,-0.5f,  0,0,-1, 0,1,
     0.5f,-0.5f,-0.5f,  0,0,-1, 0,0,
    // Top face
    -0.5f, 0.5f, 0.5f,  0,1,0,  0,1,
     0.5f, 0.5f, 0.5f,  0,1,0,  1,1,
     0.5f, 0.5f,-0.5f,  0,1,0,  1,0,
    -0.5f, 0.5f,-0.5f,  0,1,0,  0,0,
    // Bottom face
    -0.5f,-0.5f, 0.5f,  0,-1,0, 1,1,
    -0.5f,-0.5f,-0.5f,  0,-1,0, 1,0,
     0.5f,-0.5f,-0.5f,  0,-1,0, 0,0,
     0.5f,-0.5f, 0.5f,  0,-1,0, 0,1,
    // Right face
     0.5f,-0.5f, 0.5f,  1,0,0,  0,0,
     0.5f,-0.5f,-0.5f,  1,0,0,  1,0,
     0.5f, 0.5f,-0.5f,  1,0,0,  1,1,
     0.5f, 0.5f, 0.5f,  1,0,0,  0,1,
    // Left face
    -0.5f,-0.5f, 0.5f, -1,0,0,  1,0,
    -0.5f, 0.5f, 0.5f, -1,0,0,  1,1,
    -0.5f, 0.5f,-0.5f, -1,0,0,  0,1,
    -0.5f,-0.5f,-0.5f, -1,0,0,  0,0,
};
unsigned int cube_indices[] = {
    0,1,2, 2,3,0,      // front
    4,5,6, 6,7,4,      // back
    8,9,10, 10,11,8,   // top
    12,13,14, 14,15,12,// bottom
    16,17,18, 18,19,16,// right
    20,21,22, 22,23,20 // left
};

// Sphere mesh generation (positions, normals, texcoords, indices)
#define SPHERE_LAT 16
#define SPHERE_LON 32
float sphere_vertices[(SPHERE_LAT+1)*(SPHERE_LON+1)*8];
unsigned int sphere_indices[SPHERE_LAT*SPHERE_LON*6];
void generate_sphere_mesh() {
    int v = 0;
    for (int i = 0; i <= SPHERE_LAT; ++i) {
        float lat = (float)i / SPHERE_LAT * 3.1415926f;
        float y = cosf(lat);
        float r = sinf(lat);
        for (int j = 0; j <= SPHERE_LON; ++j) {
            float lon = (float)j / SPHERE_LON * 2.0f * 3.1415926f;
            float x = r * cosf(lon);
            float z = r * sinf(lon);
            // Position
            sphere_vertices[v++] = x * 0.4f;
            sphere_vertices[v++] = y * 0.4f;
            sphere_vertices[v++] = z * 0.4f;
            // Normal
            sphere_vertices[v++] = x;
            sphere_vertices[v++] = y;
            sphere_vertices[v++] = z;
            // Texcoord
            sphere_vertices[v++] = (float)j / SPHERE_LON;
            sphere_vertices[v++] = (float)i / SPHERE_LAT;
        }
    }
    int idx = 0;
    for (int i = 0; i < SPHERE_LAT; ++i) {
        for (int j = 0; j < SPHERE_LON; ++j) {
            int first = i * (SPHERE_LON + 1) + j;
            int second = first + SPHERE_LON + 1;
            sphere_indices[idx++] = first;
            sphere_indices[idx++] = second;
            sphere_indices[idx++] = first + 1;
            sphere_indices[idx++] = second;
            sphere_indices[idx++] = second + 1;
            sphere_indices[idx++] = first + 1;
        }
    }
}

// --- Matrix Helper Functions ---
void mat4_identity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void vec3_cross(float* out, const float* a, const float* b) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float vec3_dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void vec3_normalize(float* v) {
    float len = sqrtf(vec3_dot(v, v));
    if (len > 1e-6f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

void mat4_lookAt(float* out, const float* eye, const float* center, const float* up) {
    float f[3], s[3], u[3];
    f[0] = center[0] - eye[0]; f[1] = center[1] - eye[1]; f[2] = center[2] - eye[2];
    vec3_normalize(f);
    vec3_cross(s, f, up);
    vec3_normalize(s);
    vec3_cross(u, s, f); // No need to normalize u if s and f are orthonormal

    mat4_identity(out);
    out[0] = s[0];  out[4] = s[1];  out[8] = s[2];
    out[1] = u[0];  out[5] = u[1];  out[9] = u[2];
    out[2] = -f[0]; out[6] = -f[1]; out[10] = -f[2];
    out[12] = -vec3_dot(s, eye);
    out[13] = -vec3_dot(u, eye);
    out[14] = vec3_dot(f, eye);
}

void mat4_ortho(float* out, float left, float right, float bottom, float top, float nearVal, float farVal) {
    mat4_identity(out);
    out[0] = 2.0f / (right - left);
    out[5] = 2.0f / (top - bottom);
    out[10] = -2.0f / (farVal - nearVal);
    out[12] = -(right + left) / (right - left);
    out[13] = -(top + bottom) / (top - bottom);
    out[14] = -(farVal + nearVal) / (farVal - nearVal);
}

void mat4_perspective(float* out, float fovy, float aspect, float nearVal, float farVal) {
    float f = 1.0f / tanf(fovy / 2.0f);
    mat4_identity(out);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (farVal + nearVal) / (nearVal - farVal);
    out[11] = -1.0f;
    out[14] = (2.0f * farVal * nearVal) / (nearVal - farVal);
    out[15] = 0.0f; // Important: Set w to 0 for perspective projection
}

void mat4_multiply(float* out, const float* a, const float* b) {
    float temp[16];
    for (int i = 0; i < 4; ++i) { // row
        for (int j = 0; j < 4; ++j) { // col
            temp[i * 4 + j] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                temp[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
            }
        }
    }
    memcpy(out, temp, sizeof(temp));
}

// --- End Matrix Helper Functions ---

// --- Draw Cubes Function ---
void drawCubes(GLuint shader) {
    int gridX = 10;
    int gridZ = 5;
    float spacing = 1.1f;
    int centerI = gridX / 2;
    int centerJ = gridZ / 2;
    GLint useTextureLoc = glGetUniformLocation(shader, "useTexture");
    GLint objectColorLoc = glGetUniformLocation(shader, "objectColor");
    if (useTextureLoc != -1) glUniform1i(useTextureLoc, 1);
    for (int i = 0; i < gridX; ++i) {
        for (int j = 0; j < gridZ; ++j) {
            float model[16];
            mat4_identity(model);
            float xPos = (i - (gridX - 1) / 2.0f) * spacing;
            float zPos = (j - (gridZ - 1) / 2.0f) * spacing;
            model[12] = xPos;
            // Lift the center cube and make it yellow
            if (i == centerI && j == centerJ) {
                model[13] = 1.0f;
                if (objectColorLoc != -1) glUniform3f(objectColorLoc, 1.0f, 1.0f, 0.0f); // Yellow
            } else {
                model[13] = 0.0f;
                if (objectColorLoc != -1) glUniform3f(objectColorLoc, 1.0f, 1.0f, 1.0f); // White
            }
            model[14] = zPos;
            glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, model);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }
    }
}
// --- End Draw Cubes Function ---

// --- Draw Sphere Function ---
void drawSphere(GLuint shader) {
    float t = (float)glfwGetTime();
    float sphere_x = 2.0f * sinf(t);
    float sphere_y = 2.0f;
    float sphere_z = 0.0f;
    float sphere_model[16];
    mat4_identity(sphere_model);
    sphere_model[12] = sphere_x;
    sphere_model[13] = sphere_y;
    sphere_model[14] = sphere_z;
    GLint useTextureLoc = glGetUniformLocation(shader, "useTexture");
    GLint objectColorLoc = glGetUniformLocation(shader, "objectColor");
    if (useTextureLoc != -1) glUniform1i(useTextureLoc, 0);
    if (objectColorLoc != -1) glUniform3f(objectColorLoc, 1.0f, 0.5f, 0.0f);
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, sphere_model);
    glDrawElements(GL_TRIANGLES, SPHERE_LAT * SPHERE_LON * 6, GL_UNSIGNED_INT, 0);
}
// --- End Draw Sphere Function ---

int main(void) {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(640, 480, "Rotating 3D Cube", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        printf("Failed to initialize GLAD\n");
        return -1;
    }
    glEnable(GL_DEPTH_TEST);

    // Register input callbacks
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);

    GLuint shader = create_program("vertex_shader.glsl", "fragment_shader.glsl");
    depthShaderProgram = create_program("depth_vertex_shader.glsl", "depth_fragment_shader.glsl"); // Compile depth shader

    // Setup cube VAO/VBO/EBO
    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_vertices), cube_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cube_indices), cube_indices, GL_STATIC_DRAW);
    // positions
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // normals
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // texcoords
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // Load texture
    int tex_w, tex_h, tex_channels;
    unsigned char* tex_data = stbi_load("rock_texture.bmp", &tex_w, &tex_h, &tex_channels, 3);
    if (!tex_data) { printf("Failed to load texture!\n"); return -1; }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex_w, tex_h, 0, GL_RGB, GL_UNSIGNED_BYTE, tex_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(tex_data);

    // Sphere VAO/VBO/EBO
    generate_sphere_mesh();
    GLuint sphereVAO, sphereVBO, sphereEBO;
    glGenVertexArrays(1, &sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);
    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sphere_vertices), sphere_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sphere_indices), sphere_indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // --- Shadow Map FBO Setup ---
    glGenFramebuffers(1, &depthMapFBO);

    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Clamp to border helps prevent sampling outside the map
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f }; // Areas outside shadow map are lit
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);


    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE); // We don't need to draw color data
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("ERROR::FRAMEBUFFER:: Framebuffer is not complete!\n");
        glfwTerminate();
        return -1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // --- End Shadow Map FBO Setup ---

    double lastTime = glfwGetTime();
    int nbFrames = 0;
    char title[64];

    while (!glfwWindowShouldClose(window)) {
        float t = (float)glfwGetTime();

        // --- Shadow Mapping Pass ---
        float lightPos[3] = {-5.0f, 10.0f, -3.0f}; // Position the light source
        float lightProjection[16], lightView[16];
        float lightSpaceMatrix[16];
        float near_plane = 1.0f, far_plane = 20.0f;
        // Using Orthographic projection for directional light
        mat4_ortho(lightProjection, -15.0f, 15.0f, -15.0f, 15.0f, near_plane, far_plane);
        mat4_lookAt(lightView, lightPos, (float[3]){0.0f, 0.0f, 0.0f}, (float[3]){0.0f, 1.0f, 0.0f});

        // Matrix multiplication: lightSpaceMatrix = lightProjection * lightView
        // Simple implementation (replace with a proper matrix mul function if available)
        //memset(lightSpaceMatrix, 0, sizeof(lightSpaceMatrix));
        //for(int i=0; i<4; ++i) { // row
        //    for(int j=0; j<4; ++j) { // col
        //        for(int k=0; k<4; ++k) {
        //             lightSpaceMatrix[i*4 + j] += lightProjection[i*4 + k] * lightView[k*4 + j];
        //        }
        //    }
        //}
        mat4_multiply(lightSpaceMatrix, lightProjection, lightView); // Use helper function

        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); // Enable depth testing for depth map generation
        glEnable(GL_CULL_FACE); // Cull front faces to prevent shadow acne
        glCullFace(GL_FRONT);

        // Render scene from light's perspective
        glUseProgram(depthShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(depthShaderProgram, "lightSpaceMatrix"), 1, GL_FALSE, lightSpaceMatrix);

        glBindVertexArray(VAO);       // Bind Cube VAO
        drawCubes(depthShaderProgram); // Render cubes
        glBindVertexArray(sphereVAO); // Bind Sphere VAO
        drawSphere(depthShaderProgram); // Render sphere
        glBindVertexArray(0);         // Unbind VAO
        glCullFace(GL_BACK); // Restore backface culling
        glDisable(GL_CULL_FACE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind FBO
        // --- End Shadow Mapping Pass ---


        // --- Main Rendering Pass ---
        // Reset viewport
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); // Set background color
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);


        glUseProgram(shader);

        // Bind shadow map texture to texture unit 1
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthMap);
        glUniform1i(glGetUniformLocation(shader, "shadowMap"), 1);

        // Bind regular texture to texture unit 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(glGetUniformLocation(shader, "texture1"), 0);

        glUniform3f(glGetUniformLocation(shader, "objectColor"), 1.0f, 1.0f, 1.0f); // Default color (overridden in drawCubes or drawSphere)

        // Matrices (Camera View/Projection)
        float aspect = (float)display_w / (float)display_h;
        float fov = 45.0f * 3.1415926f / 180.0f;
        float znear = 0.1f, zfar = 50.0f;
        float proj[16];
        mat4_perspective(proj, fov, aspect, znear, zfar);
        // View matrix (camera)
        float cam_pitch_rad = cam_pitch * 3.1415926f / 180.0f;
        float cam_yaw_rad = cam_yaw * 3.1415926f / 180.0f;
        float cam_dist_rad = cam_dist * 3.1415926f / 180.0f;
        float cx = cam_dist * cosf(cam_pitch_rad) * sinf(cam_yaw_rad);
        float cy = cam_dist * sinf(cam_pitch_rad);
        float cz = cam_dist * cosf(cam_pitch_rad) * cosf(cam_yaw_rad);
        float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        float eye[3] = {cx, cy + 2.0f, cz};
        float center[3] = {0, 0, 0};
        float up[3] = {0, 1, 0};
        float fwd[3] = {center[0]-eye[0], center[1]-eye[1], center[2]-eye[2]};
        float fwd_len = sqrtf(fwd[0]*fwd[0]+fwd[1]*fwd[1]+fwd[2]*fwd[2]);
        for(int i=0;i<3;++i) fwd[i]/=fwd_len;
        float s[3] = {fwd[1]*up[2]-fwd[2]*up[1], fwd[2]*up[0]-fwd[0]*up[2], fwd[0]*up[1]-fwd[1]*up[0]};
        float s_len = sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]);
        for(int i=0;i<3;++i) s[i]/=s_len;
        float u[3] = {s[1]*fwd[2]-s[2]*fwd[1], s[2]*fwd[0]-s[0]*fwd[2], s[0]*fwd[1]-s[1]*fwd[0]};
        view[0]=s[0]; view[4]=s[1]; view[8]=s[2];
        view[1]=u[0]; view[5]=u[1]; view[9]=u[2];
        view[2]=-fwd[0]; view[6]=-fwd[1]; view[10]=-fwd[2];
        view[12]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
        view[13]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
        view[14]=fwd[0]*eye[0]+fwd[1]*eye[1]+fwd[2]*eye[2];
        mat4_lookAt(view, eye, center, up); // Use helper function for view matrix

        // Set uniforms
        // model matrix is set inside drawCubes or drawSphere
        glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, view);
        glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, proj);
        glUniformMatrix4fv(glGetUniformLocation(shader, "lightSpaceMatrix"), 1, GL_FALSE, lightSpaceMatrix); // Pass light space matrix

        // Light direction (normalized) - Use the same direction derived from lightPos
        float lightDir[3] = {-lightPos[0], -lightPos[1], -lightPos[2]};
        vec3_normalize(lightDir);
        glUniform3fv(glGetUniformLocation(shader, "lightDir"), 1, lightDir);
        glUniform3fv(glGetUniformLocation(shader, "viewPos"), 1, eye); // Pass camera position


        // Render scene normally using main shader
        glBindVertexArray(VAO);       // Bind Cube VAO
        drawCubes(shader);          // Render cubes
        glBindVertexArray(sphereVAO); // Bind Sphere VAO
        drawSphere(shader);          // Render sphere
        glBindVertexArray(0);         // Unbind VAO


        glfwSwapBuffers(window);
        glfwPollEvents();

        // FPS counter
        nbFrames++;
        double currentTime = glfwGetTime();
        if (currentTime - lastTime >= 1.0) {
            snprintf(title, sizeof(title), "Rotating 3D Cube [FPS: %d]", nbFrames);
            glfwSetWindowTitle(window, title);
            nbFrames = 0;
            lastTime += 1.0;
        }
    }

    // Cleanup
    glDeleteFramebuffers(1, &depthMapFBO);
    glDeleteTextures(1, &depthMap);
    glDeleteProgram(depthShaderProgram);
    glDeleteProgram(shader);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    glDeleteTextures(1, &tex);

    glfwTerminate();
    return 0;
}
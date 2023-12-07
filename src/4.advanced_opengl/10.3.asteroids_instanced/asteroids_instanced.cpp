#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <learnopengl/filesystem.h>
#include <learnopengl/shader.h>
#include <learnopengl/camera.h>
#include <learnopengl/model.h>

#include <iostream>
#include <chrono>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);

// settings
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

// camera
//Camera camera(glm::vec3(0.0f, 0.0f, 155.0f));
Camera camera(glm::vec3(-275.0f, 165.0f, 200.0f));
float lastX = (float)SCR_WIDTH / 2.0;
float lastY = (float)SCR_HEIGHT / 2.0;
bool firstMouse = true;

// roate group
int rotate_group = 8;
int rotate_limit = 1;

// timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// profiler
typedef std::chrono::duration<size_t, std::nano> Duration;
typedef std::chrono::high_resolution_clock::time_point TimePoint;
constexpr auto now = chrono::high_resolution_clock::now;
struct FStat {
    // 无量纲的cost
    double m_cost = 0;
    void Update(double cost) {
        m_cost = m_cost * 0.8 + cost * 0.2;
    }
    // 毫秒cost
    void Update(Duration duration) {
        auto mms = std::chrono::duration_cast<std::chrono::microseconds>(duration);
        Update(mms.count()*0.001);
    }
};



FStat g_cpu_cost_per_update_pos;
FStat g_cpu_cost_bind_sub_data;
FStat g_total_delta;

int main()
{
    // glfw: initialize and configure
    // ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

    // glfw window creation
    // --------------------
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    // tell GLFW to capture our mouse
    //glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // configure global opengl state
    // -----------------------------
    glEnable(GL_DEPTH_TEST);

    // build and compile shaders
    // -------------------------
    Shader asteroidShader("10.3.asteroids.vs", "10.3.asteroids.fs");
    Shader planetShader("10.3.planet.vs", "10.3.planet.fs");

    // load models
    // -----------
    Model rock(FileSystem::getPath("resources/objects/rock/rock.obj"));
    Model planet(FileSystem::getPath("resources/objects/planet/planet.obj"));

    // generate a large list of semi-random model transformation matrices
    // ------------------------------------------------------------------
    unsigned int amount = 100000;
    int nums_per_group = amount / rotate_group;
    glm::mat4* modelMatrices;
    modelMatrices = new glm::mat4[amount];
    srand(glfwGetTime()); // initialize random seed	
    float Radius = 150.0;
    float offset = 4.0f;
    float gap_size = 15.f;
    for (unsigned int i = 0; i < amount; i++)
    {
        int index = i / nums_per_group;
        float radius = Radius - index * gap_size;
        glm::mat4 model = glm::mat4(1.0f);
        // 1. translation: displace along circle with 'radius' in range [-offset, offset]
        float angle = (float)i / (float)amount * 360.0f;
        float displacement = (rand() % (int)(2 * offset * 100)) / 100.0f - offset;
        float x = sin(angle) * radius + displacement;
        displacement = (rand() % (int)(2 * offset * 100)) / 100.0f - offset;
        float y = displacement * 0.4f; // keep height of asteroid field smaller compared to width of x and z
        displacement = (rand() % (int)(2 * offset * 100)) / 100.0f - offset;
        float z = cos(angle) * radius + displacement;
        model = glm::translate(model, glm::vec3(x, y, z));

        // 2. scale: Scale between 0.05 and 0.25f
        float scale = (rand() % 20) / 100.0f + 0.05;
        model = glm::scale(model, glm::vec3(scale));

        // 3. rotation: add random rotation around a (semi)randomly picked rotation axis vector
        float rotAngle = (rand() % 360);
        model = glm::rotate(model, rotAngle, glm::vec3(0.4f, 0.6f, 0.8f));

        // 4. now add to list of matrices
        modelMatrices[i] = model;
    }

    // configure instanced array
    // -------------------------
    unsigned int buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, amount * sizeof(glm::mat4), &modelMatrices[0], GL_STATIC_DRAW);

    // set transformation matrices as an instance vertex attribute (with divisor 1)
    // note: we're cheating a little by taking the, now publicly declared, VAO of the model's mesh(es) and adding new vertexAttribPointers
    // normally you'd want to do this in a more organized fashion, but for learning purposes this will do.
    // -----------------------------------------------------------------------------------------------------------------------------------
    for (unsigned int i = 0; i < rock.meshes.size(); i++)
    {
        unsigned int VAO = rock.meshes[i].VAO;
        glBindVertexArray(VAO);
        // set attribute pointers for matrix (4 times vec4)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)0);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(sizeof(glm::vec4)));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(2 * sizeof(glm::vec4)));
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(3 * sizeof(glm::vec4)));

        glVertexAttribDivisor(3, 1);
        glVertexAttribDivisor(4, 1);
        glVertexAttribDivisor(5, 1);
        glVertexAttribDivisor(6, 1);

        glBindVertexArray(0);
    }

    auto UpdatePos = [=] {
        auto tt1 = now();
        glm::mat4 rot = glm::rotate(glm::mat4(1), 0.002f, glm::vec3(0, 1, 0));
        int start = 0;
        int end = std::min((int)amount, 0 + nums_per_group * rotate_limit);
        for (unsigned int i = start; i < end; i++)
        {
            glm::mat4 model = modelMatrices[i];
            modelMatrices[i] = rot * model;
        }
        auto tt2 = now();
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferSubData(GL_ARRAY_BUFFER, sizeof(glm::mat4) * start, (end - start) * sizeof(glm::mat4), &modelMatrices[start]);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        auto tt3 = now();
        g_cpu_cost_per_update_pos.Update(tt3 - tt1);
        g_cpu_cost_bind_sub_data.Update(tt3 - tt2);
    };
    camera.MovementSpeed = 250;
    camera.Yaw = -36;
    camera.Pitch = -26;
    camera.updateCameraVectors();
    // render loop
    // -----------
    while (!glfwWindowShouldClose(window))
    {
        // per-frame time logic
        // --------------------
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        g_total_delta.Update(deltaTime *1000);
        UpdatePos();

        // input
        // -----
        processInput(window);

        // render
        // ------
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // configure transformation matrices
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);
        glm::mat4 view = camera.GetViewMatrix();
        asteroidShader.use();
        asteroidShader.setMat4("projection", projection);
        asteroidShader.setMat4("view", view);
        planetShader.use();
        planetShader.setMat4("projection", projection);
        planetShader.setMat4("view", view);
        
        // draw planet
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(0.0f, -3.0f, 0.0f));
        model = glm::scale(model, glm::vec3(4.0f, 4.0f, 4.0f));
        planetShader.setMat4("model", model);
        planet.Draw(planetShader);

        // draw meteorites
        asteroidShader.use();
        asteroidShader.setInt("texture_diffuse1", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rock.textures_loaded[0].id); // note: we also made the textures_loaded vector public (instead of private) from the model class.
        for (unsigned int i = 0; i < rock.meshes.size(); i++)
        {
            glBindVertexArray(rock.meshes[i].VAO);
            glDrawElementsInstanced(GL_TRIANGLES, rock.meshes[i].indices.size(), GL_UNSIGNED_INT, 0, amount);
            glBindVertexArray(0);
        }

        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

int g_last_key_states[GLFW_KEY_LAST] = {0};

void ResetKeyStates() {
    for (int& state : g_last_key_states) {
        state = 0;
    }
}

bool CheckIsKeyClicked(GLFWwindow* window, int key) {
    int cur_state = glfwGetKey(window, key);
    int last_state = g_last_key_states[key];
    if (cur_state == GLFW_RELEASE) {
        g_last_key_states[key] = cur_state;
        if (last_state == GLFW_PRESS) {
            return true;
        }
    }
    else if (cur_state == GLFW_PRESS) {
        g_last_key_states[key] = cur_state;
    }
    return false;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow *window)
{
    int mode = glfwGetInputMode(window, GLFW_CURSOR);
    // 普通光标模式不响应其他按键
    if (mode == GLFW_CURSOR_NORMAL) {
        return;
    }
    
    // 退出控制
    if (CheckIsKeyClicked(window, GLFW_KEY_TAB)) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        ResetKeyStates();
        return;
    }

    if (CheckIsKeyClicked(window, GLFW_KEY_P)) {
        auto Position = camera.Position;
        std::cout << "Debug Info:" << std::endl;
        std::cout << "camera.Position = " << Position.x << ", " << Position.y << ", " << Position.z << std::endl;
        std::cout << "camera.eluer_angle Yaw:" << camera.Yaw << " Pitch" << camera.Pitch << std::endl;
        std::cout << "g_cpu_cost_per_update_pos = " << g_cpu_cost_per_update_pos.m_cost << std::endl;
        std::cout << "g_cpu_cost_bind_sub_data = " << g_cpu_cost_bind_sub_data.m_cost << std::endl;
        std::cout << "g_total_delta = " << g_total_delta.m_cost << std::endl;
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.ProcessKeyboard(RIGHT, deltaTime);

    if (CheckIsKeyClicked(window, GLFW_KEY_PAGE_UP)) {
        rotate_limit++;
        rotate_limit = glm::min(rotate_limit, rotate_group);
    }
    else if (CheckIsKeyClicked(window, GLFW_KEY_PAGE_DOWN)) {
        rotate_limit--;
        rotate_limit = glm::max(rotate_limit, 0);
    }
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and 
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}

// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    int mode = glfwGetInputMode(window, GLFW_CURSOR);
    if (mode == GLFW_CURSOR_NORMAL) {
        firstMouse = true; // 重置掉
        return;
    }
    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        // enter game anyway
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        double xpos, ypos;
        //getting cursor position
        glfwGetCursorPos(window, &xpos, &ypos);
        cout << "Click at " << xpos << " : " << ypos << endl;
    }
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    int mode = glfwGetInputMode(window, GLFW_CURSOR);
    // 普通光标模式不响应其他按键
    if (mode == GLFW_CURSOR_NORMAL) {
        return;
    }

    float delta = (camera.MovementSpeed/20);
    delta = glm::clamp(delta, 1.0f, 100.0f);
    delta = delta * yoffset;
    camera.MovementSpeed += delta;
    camera.MovementSpeed = glm::clamp(camera.MovementSpeed, 2.5f, 500.0f);
    cout << "camera.MovementSpeed " << camera.MovementSpeed << endl;
    //camera.ProcessMouseScroll(yoffset);
}

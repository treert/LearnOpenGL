#version 330 core
out vec4 FragColor;

in vec3 ourColor;

void main()
{
    //FragColor = vec4(ourColor, 1.0f);
    FragColor = vec4(ourColor, ourColor.r*0.2+ourColor.g*0.5);
}
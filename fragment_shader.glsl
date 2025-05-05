#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 FragPosLightSpace;

uniform sampler2D texture1;
uniform sampler2D shadowMap;
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 objectColor;
uniform int useTexture;

float calculateShadow(vec4 fragPosLightSpace)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    float closestDepth = texture(shadowMap, projCoords.xy).r; 
    float currentDepth = projCoords.z;
    float bias = max(0.001 * (1.0 - dot(Normal, -lightDir)), 0.0001);  
    float shadow = 0.0;
	vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
	for(int x = -1; x <= 1; ++x) {
		for(int y = -1; y <= 1; ++y) {
			float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
			shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
		}
	}
	shadow /= 9.0;

    if(projCoords.z > 1.0) shadow = 0.0;

    return shadow;
}

void main()
{
    vec3 color = useTexture == 1 ? texture(texture1, TexCoord).rgb : objectColor;
    vec3 norm = normalize(Normal);
    vec3 lightColor = vec3(1.0);

    float ambientStrength = 0.2;
    vec3 ambient = ambientStrength * lightColor;

    float diff = max(dot(norm, -lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * lightColor;    
    
    float shadow = calculateShadow(FragPosLightSpace);

    vec3 lighting = ambient + (1.0 - shadow) * (diffuse + specular);
    FragColor = vec4(lighting * color, 1.0);
} 
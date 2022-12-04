struct directional_light {
    vec3 direction;
    vec4 colour;
};

struct point_light {
    vec3 position;
    vec4 colour;
    // Usually 1, make sure denominator never gets smaller than 1
    float constant_f;
    // Reduces light intensity linearly
    float linear;
    // Makes the light fall off slower at longer distances.
    float quadratic;
};
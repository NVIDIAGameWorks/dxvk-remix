// AgX implementation based on https://github.com/MrLixm/AgXc

// AgX Base Transform (Rec.2020 primaries to AgX working space)
static const float3x3 AgX_InputTransform = 
{
    {0.842479062253094, 0.0784335999999992,  0.0792237451477643},
    {0.0423282422610123, 0.878468636469772,  0.0791661274605434},
    {0.0423756549057051, 0.0784336,          0.879142973793104}
};

// AgX Base Transform Output (AgX working space to Rec.2020 primaries)
static const float3x3 AgX_OutputTransform =
{
    { 1.19687900512017, -0.0980208811401368, -0.0990297440797205},
    {-0.0528968517574562, 1.15190312639708,   -0.0989611768448433},
    {-0.0529716355144438, -0.0980434501171241, 1.15107367264116}
};

// AgX curve implementation
float agx_default_contrast_approx(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    float x6 = x4 * x2;
    
    return + 15.5 * x6
           - 40.14 * x4 * x
           + 31.96 * x4
           - 6.868 * x2 * x
           + 0.4298 * x2
           + 0.1191 * x
           - 0.00232;
}

// AgX Look transforms
float3 agx_look_punchy(float3 val) {
    const float3x3 punchy = {
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0}, 
        {0.0, 0.0, 1.0}
    };
    
    // Increase contrast and saturation
    val = pow(val, 0.7);
    val = lerp(dot(val, float3(0.2126, 0.7152, 0.0722)), val, 1.4);
    return val;
}

float3 agx_look_golden(float3 val) {
    // Golden hour look - warm highlights, cooler shadows
    float luma = dot(val, float3(0.2126, 0.7152, 0.0722));
    float3 warm = val * float3(1.1, 1.05, 0.9);
    float3 cool = val * float3(0.9, 0.95, 1.1);
    return lerp(cool, warm, smoothstep(0.2, 0.8, luma));
}

float3 agx_look_greyscale(float3 val) {
    float luma = dot(val, float3(0.2126, 0.7152, 0.0722));
    return float3(luma, luma, luma);
}

float3 apply_agx_look(float3 val, int look) {
    switch(look) {
        case 1: return agx_look_punchy(val);
        case 2: return agx_look_golden(val);
        case 3: return agx_look_greyscale(val);
        default: return val;
    }
}

float3 AgXToneMapping(float3 color, float gamma, float saturation, float exposureOffset, 
                      int look, float contrast, float slope, float power) {
    // Apply exposure offset
    color = color * pow(2.0, exposureOffset);
    
    // Input transform to AgX working space
    color = mul(AgX_InputTransform, color);
    
    // Ensure positive values for log transform
    color = max(color, 1e-10);
    
    // Convert to log2 space
    color = log2(color);
    
    // AgX constants
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;
    
    // Normalize to [0, 1] range
    color = (color - min_ev) / (max_ev - min_ev);
    color = saturate(color);
    
    // Apply contrast and slope adjustments
    color = pow(color, 1.0 / contrast);
    color = color * slope;
    
    // Apply the AgX curve per channel
    color.r = agx_default_contrast_approx(color.r);
    color.g = agx_default_contrast_approx(color.g);
    color.b = agx_default_contrast_approx(color.b);
    
    // Apply power adjustment for midtone response
    color = pow(saturate(color), power);
    
    // Output transform back to display space
    color = mul(AgX_OutputTransform, color);
    
    // Apply look
    color = apply_agx_look(color, look);
    
    // Apply gamma correction
    color = pow(saturate(color), 1.0 / gamma);
    
    // Apply saturation adjustment
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma, luma, luma), color, saturation);
    
    return saturate(color);
}
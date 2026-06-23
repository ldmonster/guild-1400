#version 450
// guild — 3D scene fragment shader. Output = texel * vColor, exactly the engine's
// fixed-function MODULATE (render::RasterizeDrawList: cr = colorModulator * texel).
// The bound sampler is NEAREST + REPEAT, so frac(uv)*w == the engine's per-axis
// floor(uv*w) mod w (render::SampleTexel). Untextured batches bind a 1x1 white
// texel, so the result reduces to vColor (the baked/flat surface colour).
//
// gl_FragDepth is written from the noperspective view-space z so the GPU depth
// test orders fragments identically to the CPU z-buffer (which compares linear vz).

layout(location = 0) noperspective in vec3 vColor;
layout(location = 1) smooth        in vec2 vUV;
layout(location = 2) noperspective in float vViewZ;

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform PC {
    vec4 eye, right, up, fwd, proj0, proj1, proj2, proj3;  // proj3.w = farRef
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 texel = texture(uTex, vUV).rgb;     // BGRA8 image -> .rgb = R,G,B in 0..1
    // pc.fwd.w carries the per-batch transparency opacity (gilde.exe 0x5e0358: the
    // material byte1/255). Opaque batches push 1.0; transparent pipelines blend with
    // SRC_ALPHA so the additive contribution is (vColor*texel)*opacity (CPU-identical).
    outColor = vec4(vColor * texel, pc.fwd.w);
    float farRef = pc.proj3.w;
    gl_FragDepth = clamp(vViewZ / farRef, 0.0, 1.0);
}

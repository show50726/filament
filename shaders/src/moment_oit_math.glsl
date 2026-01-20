//------------------------------------------------------------------------------
// Moment-Based Order-Independent Transparency Math
//------------------------------------------------------------------------------

float computeTransmittanceAtDepthFrom4PowerMoments(float b_0, vec2 b_even, vec2 b_odd, float depth, float bias, float overestimation, vec4 bias_vector)
{
    vec4 b = vec4(b_odd.x, b_even.x, b_odd.y, b_even.y);
    // Bias input data to avoid artifacts
    b = mix(b, bias_vector, bias);
    vec3 z;
    z[0] = depth;

    // Compute a Cholesky factorization of the Hankel matrix B storing only non-
    // trivial entries or related products
    float L21D11 = -b[0] * b[1] + b[2]; // mad(-b[0], b[1], b[2])
    float D11 = -b[0] * b[0] + b[1];    // mad(-b[0], b[0], b[1])
    float InvD11 = 1.0 / D11;
    float L21 = L21D11 * InvD11;
    float SquaredDepthVariance = -b[1] * b[1] + b[3]; // mad(-b[1], b[1], b[3])
    float D22 = -L21D11 * L21 + SquaredDepthVariance; // mad(-L21D11, L21, SquaredDepthVariance)

    // Obtain a scaled inverse image of bz=(1,z[0],z[0]*z[0])^T
    vec3 c = vec3(1.0, z[0], z[0] * z[0]);
    // Forward substitution to solve L*c1=bz
    c[1] -= b.x;
    c[2] -= b.y + L21 * c[1];
    // Scaling to solve D*c2=c1
    c[1] *= InvD11;
    c[2] /= D22;
    // Backward substitution to solve L^T*c3=c2
    c[1] -= L21 * c[2];
    c[0] -= dot(c.yz, b.xy);
    // Solve the quadratic equation c[0]+c[1]*z+c[2]*z^2 to obtain solutions
    // z[1] and z[2]
    float InvC2 = 1.0 / c[2];
    float p = c[1] * InvC2;
    float q = c[0] * InvC2;
    float D = (p * p * 0.25) - q;
    float r = sqrt(D);
    z[1] = -p * 0.5 - r;
    z[2] = -p * 0.5 + r;
    // Compute the absorbance by summing the appropriate weights
    vec3 polynomial;
    vec3 weight_factor = vec3(overestimation, (z[1] < z[0]) ? 1.0 : 0.0, (z[2] < z[0]) ? 1.0 : 0.0);
    float f0 = weight_factor[0];
    float f1 = weight_factor[1];
    float f2 = weight_factor[2];
    float f01 = (f1 - f0) / (z[1] - z[0]);
    float f12 = (f2 - f1) / (z[2] - z[1]);
    float f012 = (f12 - f01) / (z[2] - z[0]);
    polynomial[0] = f012;
    polynomial[1] = polynomial[0];
    polynomial[0] = f01 - polynomial[0] * z[1];
    polynomial[2] = polynomial[1];
    polynomial[1] = polynomial[0] - polynomial[1] * z[0];
    polynomial[0] = f0 - polynomial[0] * z[0];
    float absorbance = polynomial[0] + dot(b.xy, polynomial.yz);
    // Turn the normalized absorbance into transmittance
    return saturate(exp(-b_0 * absorbance));
}

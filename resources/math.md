# Math Reference for 3D Graphics

## Coordinate Systems
- Vulkan: right-handed, Y points down in NDC, Z range [0, 1]
- GLM: right-handed by default, use GLM_FORCE_DEPTH_ZERO_TO_ONE for Vulkan

## Transforms
- Model Matrix: object space → world space (translate, rotate, scale)
- View Matrix: world space → camera space (inverse of camera transform)
- Projection Matrix: camera space → clip space (perspective divide happens after)
- MVP = Projection * View * Model (applied right to left)

## Perspective Projection
```
f = 1 / tan(fov/2)
aspect = width / height

| f/aspect  0    0              0             |
| 0         f    0              0             |
| 0         0    far/(near-far) near*far/(near-far) |
| 0         0   -1              0             |
```
After multiplication, divide x,y,z by w (which equals -z). That's the perspective divide.

## Quaternions
- q = w + xi + yj + zk
- Avoids gimbal lock, interpolates smoothly (SLERP)
- Compose rotations: q_combined = q2 * q1 (apply q1 first)
- GLM: glm::quat, glm::angleAxis(), glm::rotate()

## Useful Formulas
- Frustum planes from VP matrix: extract rows (Gribb & Hartmann method)
- Screen to world ray: inverse(VP) * NDC point
- Sphere-frustum test: dot(plane_normal, center) + d > -radius
- AABB-frustum test: test all 6 planes against box corners

## Normal Mapping
- TBN matrix: Tangent, Bitangent, Normal — transforms normal map from tangent space to world space
- Compute tangent/bitangent from UV gradients per triangle

## Physically Based
- Fresnel: F0 + (1 - F0) * (1 - cos(theta))^5 (Schlick approximation)
- GGX/Trowbridge-Reitz: NDF for microfacet distribution
- Smith geometry function: accounts for self-shadowing of microfacets

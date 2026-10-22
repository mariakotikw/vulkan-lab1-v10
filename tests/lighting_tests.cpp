#include "application_state.hpp"
#include "dodecahedron.hpp"
#include "light_gizmos.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool close(float a, float b) { return std::abs(a-b) < 1e-4f; }
bool close(glm::vec3 a, glm::vec3 b) { return glm::length(a-b) < 1e-4f; }
}
int main() {
	try {
		using namespace application;
		const auto topology = geometry::makeDodecahedron();
		const auto mesh = geometry::makeFlatDodecahedron();
		require(mesh.vertices.size() == 60 && mesh.indices.size() == 108, "Flat mesh counts");
		for (size_t face = 0; face < 12; ++face) {
			const auto n = mesh.vertices[face*5].normal;
			require(close(glm::length(n), 1), "Normal must have unit length");
			for (size_t v = 0; v < 5; ++v) {
				const auto& vertex = mesh.vertices[face*5+v];
				require(close(vertex.position, topology.vertices[topology.faces[face][v]].position), "Flat mesh changes geometry");
				require(close(vertex.normal, n), "Normal is not constant over a pentagon");
				require(glm::dot(vertex.normal, vertex.position) > 0, "Inward normal");
			}
			for (size_t t = face*9; t < face*9+9; t += 3) {
				const auto a = mesh.vertices.at(mesh.indices[t]).position;
				const auto b = mesh.vertices.at(mesh.indices[t+1]).position;
				const auto c = mesh.vertices.at(mesh.indices[t+2]).position;
				require(close(glm::normalize(glm::cross(b-a,c-a)), n), "Triangle winding differs from face normal");
			}
		}
		SceneState scene;
		scene.sanitize();
		auto& camera = scene.camera;
		require(close(glm::vec3(camera.viewMatrix()*glm::vec4(camera.position,1)), glm::vec3(0)), "View does not map eye to origin");
		require(close(glm::vec3(camera.viewMatrix()*glm::vec4(camera.position+camera.forward(),1)), {0,0,-1}), "View orientation");
		for (auto projection : {Projection::Perspective, Projection::Orthographic}) {
			camera.projection = projection;
			camera.near_plane = 0.5f; camera.far_plane = 25;
			const auto p = camera.projectionMatrix(1.7f);
			for (auto distance : {camera.near_plane, camera.far_plane}) {
				const auto clip = p*glm::vec4(0,0,-distance,1);
				require(close(clip.z/clip.w, distance==camera.near_plane ? 0.0f : 1.0f), "Custom clip planes are ignored");
			}
		}
		const auto original = scene.sceneUniform(1.7f).view_projection;
		camera.position.x += 1;
		require(scene.sceneUniform(1.7f).view_projection != original, "Camera position does not affect VP");
		const auto moved_view = scene.sceneUniform(1.7f).view_projection;
		camera.yaw_degrees += 30;
		require(scene.sceneUniform(1.7f).view_projection != moved_view, "Camera yaw does not affect VP");
		camera.pitch_degrees = 90; camera.near_plane = -1; camera.far_plane = -5;
		camera.sanitize();
		require(camera.pitch_degrees == 89 && camera.near_plane > 0 && camera.far_plane > camera.near_plane, "Unsafe camera state");
		camera.reset();
		require(close(camera.position, ProjectionState{}.position) && close(camera.yaw_degrees,-90), "Camera reset");
		const auto old_uniforms = scene.objectUniforms();
		scene.objects[0].transform.scale = {0.3f, 1.2f, 2.1f};
		scene.objects[0].transform.rotation_degrees = {20,31,15};
		const auto object = scene.objectUniforms()[0];
		for (size_t f = 0; f < 12; ++f) {
			const auto n = glm::normalize(glm::mat3(object.normal_matrix)*mesh.vertices[f*5].normal);
			const auto tangent = glm::mat3(object.model)*(mesh.vertices[f*5+1].position - mesh.vertices[f*5].position);
			require(std::abs(glm::dot(n,tangent)) < 1e-4f, "Inverse transpose is incorrect for nonuniform scale");
		}
		scene.objects[0].material = {{0.1f,0.5f,0.9f},{0.9f,0.2f,0.1f},127};
		const auto uniforms = scene.objectUniforms();
		require(close(glm::vec3(uniforms[0].albedo), {0.1f,0.5f,0.9f}) && close(uniforms[0].specular_shininess.w,127), "Material upload");
		require(uniforms[1].model == old_uniforms[1].model && uniforms[1].albedo == old_uniforms[1].albedo, "Material or transform leaks to other object");
		Material invalid{{-1,2,0.5f},{2,-2,0.2f},-100}; invalid.sanitize();
		require(close(invalid.albedo,{0,1,0.5f}) && close(invalid.shininess,1), "Material constraints");
		SpotLight spot;
		spot.inner_degrees=60; spot.outer_degrees=20; spot.direction={0,0,0}; spot.sanitize();
		require(spot.inner_degrees==20 && spot.outer_degrees==60 && close(glm::length(spot.direction),1), "Spot sorting/direction fallback");
		spot.inner_degrees=-10; spot.outer_degrees=180; spot.sanitize();
		require(spot.inner_degrees==0 && spot.outer_degrees==89, "Spot angle range");
		spot.inner_degrees=spot.outer_degrees=30;
		const auto gpu = spot.gpu();
		require(close(gpu.direction_inner_cos.w,gpu.outer_cos.x), "Equal cone angles");
		require(spotConeFactor(1,0.8f,0.8f)==1 && spotConeFactor(0.7f,0.8f,0.8f)==0, "Hard cone edge");
		require(close(spotConeFactor(0.8f,0.9f,0.7f),0.5f), "Soft cone midpoint");
		require(spotConeFactor(0.6f,0.9f,0.7f)==0 && spotConeFactor(1,0.9f,0.7f)==1, "Cone falloff bounds");
		require(close(inverseSquareAttenuation(4)/inverseSquareAttenuation(16),4), "Inverse square law");
		require(std::isfinite(inverseSquareAttenuation(0)) && close(inverseSquareAttenuation(0),100), "Light singularity");
		spot.direction={0,1,0}; spot.inner_degrees=std::numeric_limits<float>::quiet_NaN(); spot.sanitize();
		require(std::isfinite(spot.inner_degrees), "Non-finite spot parameter");
		scene.point_lights.clear(); scene.spot_lights.clear();
		require(scene.sceneUniform(1).light_counts == glm::uvec4(0), "Empty light counts");
		require(makeLightGizmos(scene).lines.empty() && makeLightGizmos(scene).triangles.empty(), "Empty gizmo list");
		for (int i = 0; i < 129; ++i) { scene.addPointLight(); scene.addSpotLight(); }
		scene.removePointLight(64); scene.removeSpotLight(0);
		scene.removePointLight(999); scene.removeSpotLight(999);
		require(scene.point_lights.size()==128 && scene.spot_lights.size()==128, "Dynamic add/remove");
		const auto counts = scene.sceneUniform(1).light_counts;
		require(counts.x==128 && counts.y==128, "Light count upload");
		scene.point_lights.resize(1); scene.spot_lights.resize(1);
		scene.spot_lights[0] = spot;
		const auto gizmos = makeLightGizmos(scene);
		require(gizmos.triangles.size()==72 && gizmos.lines.size()>100 && gizmos.lines.size()%2==0, "Cube or cone geometry");
		for (const auto& v : gizmos.lines) for (int i=0; i<3; ++i) require(std::isfinite(v.position[i]), "Degenerate cone basis");
		scene.spot_lights[0].outer_degrees = 75;
		const auto wider = makeLightGizmos(scene);
		require(!close(wider.lines.back().position,gizmos.lines.back().position), "Cone angle edit does not affect geometry");
		scene.show_light_gizmos = false;
		require(makeLightGizmos(scene).triangles.empty() && makeLightGizmos(scene).lines.empty(), "Gizmo toggle");
		std::cout << "PASS: flat normals; camera/view/clip/reset; inverse transpose; independent materials; "
		             "spot constraints/soft edges; inverse square; 0/128 dynamic lights; 3D gizmos; GPU ABI assertions\n";
		return 0;
	} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

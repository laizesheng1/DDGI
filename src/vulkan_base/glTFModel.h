#pragma once
#ifndef GLTFMODEL_H
#define GLTFMODEL_H

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.hpp"
#include "VKMDevice.h"
#include "Texture.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace vkmglTF {

	enum DescriptorBindingFlags {
		ImageBaseColor = 0x00000001,
		ImageNormalMap = 0x00000002
	};

	extern vk::DescriptorSetLayout descriptorSetLayoutImage;
	extern vk::DescriptorSetLayout descriptorSetLayoutUbo;
	extern vk::MemoryPropertyFlags memoryPropertyFlags;
	extern uint32_t descriptorBindingFlags;

	struct Dimensions {
		glm::vec3 min = glm::vec3(FLT_MAX);
		glm::vec3 max = glm::vec3(-FLT_MAX);
		glm::vec3 size;
		glm::vec3 center;
		float radius;
	};

	using Texture = vkm::Texture2D;
	struct Node;

/*
	glTF material class
*/
struct Material {
private:
	vkm::VKMDevice* device = nullptr;
	
public:
	enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
	AlphaMode alphaMode = ALPHAMODE_OPAQUE;

	float alphaCutoff = 1.0f;
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	glm::vec4 baseColorFactor = glm::vec4(1.0f);
	Texture* baseColorTexture = nullptr;
	Texture* metallicRoughnessTexture = nullptr;
	Texture* normalTexture = nullptr;
	Texture* occlusionTexture = nullptr;
	Texture* emissiveTexture = nullptr;

	Texture* specularGlossinessTexture;
	Texture* diffuseTexture;

	vk::DescriptorSet descriptorSet = VK_NULL_HANDLE;

	Material(vkm::VKMDevice* device) : device(device) {};
	void createDescriptorSet(vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout descriptorSetLayout, uint32_t descriptorBindingFlags);
};

/*
	glTF primitive
*/
struct Primitive {
	uint32_t firstIndex;
	uint32_t indexCount;
	uint32_t firstVertex;
	uint32_t vertexCount;
	Material& material;

	Dimensions dimensions;

	void setDimensions(glm::vec3 min, glm::vec3 max);
	Primitive(uint32_t firstIndex, uint32_t indexCount, Material& material) : firstIndex(firstIndex), indexCount(indexCount), material(material) {};
};

/*
	glTF mesh
*/
struct Mesh {
	vkm::VKMDevice* device;

	std::vector<Primitive*> primitives;
	std::string name;

	struct UniformBuffer {
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		vk::DescriptorBufferInfo descriptor;
		vk::DescriptorSet descriptorSet = VK_NULL_HANDLE;
		void* mapped;
	} uniformBuffer;

	struct UniformBlock {
		glm::mat4 matrix;
		glm::mat4 jointMatrix[64]{};
		float jointcount{ 0 };
	} uniformBlock;

	Mesh(vkm::VKMDevice* device, glm::mat4 matrix);
	~Mesh();
};

struct Skin {
	std::string name;
	Node* skeletonRoot = nullptr;
	std::vector<glm::mat4> inverseBindMatrices;
	std::vector<Node*> joints;
};

/*
	glTF node
*/
struct Node {
	Node* parent;
	uint32_t index;
	std::vector<Node*> children;
	glm::mat4 matrix;
	std::string name;
	Mesh* mesh;
	Skin* skin;
	int32_t skinIndex = -1;
	glm::vec3 translation{};
	glm::vec3 scale{ 1.0f };
	glm::quat rotation{};
	glm::mat4 localMatrix();
	glm::mat4 getMatrix();
	void update();
	~Node();
};

enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0 };

struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 color;
	glm::vec4 joint0;
	glm::vec4 weight0;
	glm::vec4 tangent;
	static vk::VertexInputBindingDescription vertexInputBindingDescription;
	static std::vector<vk::VertexInputAttributeDescription> vertexInputAttributeDescriptions;
	static vk::PipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;
	static vk::VertexInputBindingDescription inputBindingDescription(uint32_t binding);
	static vk::VertexInputAttributeDescription inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component);
	static std::vector<vk::VertexInputAttributeDescription> inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components);
	/** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
	static vk::PipelineVertexInputStateCreateInfo* getPipelineVertexInputState(const std::vector<VertexComponent> components);
};

enum FileLoadingFlags {
	None = 0x00000000,
	PreTransformVertices = 0x00000001,
	PreMultiplyVertexColors = 0x00000002,
	FlipY = 0x00000004,
	DontLoadImages = 0x00000008
};

enum RenderFlags {
	BindImages = 0x00000001,
	RenderOpaqueNodes = 0x00000002,
	RenderAlphaMaskedNodes = 0x00000004,
	RenderAlphaBlendedNodes = 0x00000008
};

class Model {
private:
	vkm::VKMDevice* device;
	Texture emptyTexture;
	bool buffersBound = false;

	Texture* getTexture(uint32_t index);
	void createEmptyTexture(vk::Queue transferQueue);
	Node* findNode(Node* parent, uint32_t index);
	Node* nodeFromIndex(uint32_t index);
	void PreCalculations(uint32_t fileLoadingFlags, std::vector<vkmglTF::Vertex>& vertexBuffer);
	void getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max);
	void getSceneDimensions();
public:
	vk::DescriptorPool descriptorPool;

	std::string path;
	std::vector<Node*> nodes;
	std::vector<Node*> linearNodes;
	std::vector<Skin*> skins;
	std::vector<Texture> textures;
	std::vector<Material> materials;

	struct Vertices {
		int count;
		vk::Buffer buffer;
		vk::DeviceMemory memory;
	} vertices;
	struct Indices {
		int count;
		vk::Buffer buffer;
		vk::DeviceMemory memory;
	} indices;
	Dimensions dimensions;

	Model() {};
	Model(vkm::VKMDevice* device_) :device(device_) {}
	~Model();
	void loadNode(vkmglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale);
	void loadSkins(tinygltf::Model& gltfModel);
	void loadImages(tinygltf::Model& gltfModel, vk::Queue transferQueue);
	void loadMaterials(tinygltf::Model& gltfModel);
	void loadFromFile(std::string filename, vk::Queue transferQueue, uint32_t fileLoadingFlags = vkmglTF::FileLoadingFlags::None, float scale = 1.0f);

	void bindBuffers(vk::CommandBuffer commandBuffer);
	void drawNode(Node* node, vk::CommandBuffer commandBuffer, uint32_t renderFlags = 0, vk::PipelineLayout pipelineLayout = VK_NULL_HANDLE, uint32_t bindImageSet = 1);
	void draw(vk::CommandBuffer commandBuffer, uint32_t renderFlags = 0, vk::PipelineLayout pipelineLayout = VK_NULL_HANDLE, uint32_t bindImageSet = 1);
	void updateNodeDescriptorSets(Node* node, vk::DescriptorSetLayout descriptorSetLayout);

};
}
#endif // !GLTFMODEL_H
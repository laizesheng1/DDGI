#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "glTFModel.h"

vk::DescriptorSetLayout vkmglTF::descriptorSetLayoutImage = VK_NULL_HANDLE;
vk::DescriptorSetLayout vkmglTF::descriptorSetLayoutUbo = VK_NULL_HANDLE;
vk::MemoryPropertyFlags vkmglTF::memoryPropertyFlags = {};
uint32_t vkmglTF::descriptorBindingFlags = vkmglTF::DescriptorBindingFlags::ImageBaseColor;

namespace vkmglTF
{

/*
We use a custom image loading function with tinyglTF, so we can do custom stuff loading ktx textures
*/
bool loadImageDataFunc(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData)
{
	// KTX files will be handled by our own code
	if (image->uri.find_last_of(".") != std::string::npos) {
		if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx") {
			return true;
		}
	}

	return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
}

bool loadImageDataFuncEmpty(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData)
{
	// This function will be used for samples that don't require images to be loaded
	return true;
}

/*
glTF material
*/
void Material::createDescriptorSet(vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout descriptorSetLayout, uint32_t descriptorBindingFlags)
{
	vk::DescriptorSetAllocateInfo descriptorSetAllocInfo;
	descriptorSetAllocInfo.setDescriptorPool(descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&descriptorSetAllocInfo, &descriptorSet));
	std::vector<vk::DescriptorImageInfo> imageDescriptors{};
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets{}; 

	if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
		imageDescriptors.push_back(baseColorTexture->descriptorImageInfo);
		vk::WriteDescriptorSet writeDescriptorSet;
		writeDescriptorSet.setDstSet(descriptorSet)
			.setDstBinding(static_cast<uint32_t>(writeDescriptorSets.size()))
			.setDescriptorCount(1)
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setImageInfo(baseColorTexture->descriptorImageInfo);

		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	if (normalTexture && descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
		imageDescriptors.push_back(normalTexture->descriptorImageInfo);
		vk::WriteDescriptorSet writeDescriptorSet;
		writeDescriptorSet.setDstSet(descriptorSet)
			.setDstBinding(static_cast<uint32_t>(writeDescriptorSets.size()))
			.setDescriptorCount(1)
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setImageInfo(normalTexture->descriptorImageInfo);
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	device->logicalDevice.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

/*
	glTF primitive
*/
void Primitive::setDimensions(glm::vec3 min, glm::vec3 max)
{
	dimensions.min = min;
	dimensions.max = max;
	dimensions.size = max - min;
	dimensions.center = (min + max) / 2.0f;
	dimensions.radius = glm::distance(min, max) / 2.0f;
}

/*
	glTF mesh
*/
Mesh::Mesh(vkm::VKMDevice* device, glm::mat4 matrix)
{
	this->device = device;
	this->uniformBlock.matrix = matrix;
	VK_CHECK_RESULT(device->createBuffer(
		vk::BufferUsageFlagBits::eUniformBuffer,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
		sizeof(uniformBlock),
		&uniformBuffer.buffer,
		&uniformBuffer.memory,
		&uniformBlock)
	);
	VK_CHECK_RESULT(device->logicalDevice.mapMemory(uniformBuffer.memory, 0, sizeof(uniformBlock), {}, &uniformBuffer.mapped));
	uniformBuffer.descriptor = vk::DescriptorBufferInfo(uniformBuffer.buffer, 0, sizeof(uniformBlock));
}

Mesh::~Mesh() {
	device->logicalDevice.destroyBuffer(uniformBuffer.buffer, nullptr);
	device->logicalDevice.freeMemory(uniformBuffer.memory, nullptr);
	for (auto primitive : primitives)
	{
		delete primitive;
	}
}

/*
	glTF node
*/
glm::mat4 Node::localMatrix()
{
	return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
}

glm::mat4 Node::getMatrix()
{
	glm::mat4 m = localMatrix();
	Node* p = parent;
	while (p) {
		m = p->localMatrix() * m;
		p = p->parent;
	}
	return m;
}

void Node::update()
{
	if (mesh) {
		glm::mat4 m = getMatrix();
		if (skin) {
			mesh->uniformBlock.matrix = m;
			// Update join matrices
			glm::mat4 inverseTransform = glm::inverse(m);
			for (size_t i = 0; i < skin->joints.size(); i++) {
				Node* jointNode = skin->joints[i];
				glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
				jointMat = inverseTransform * jointMat;
				mesh->uniformBlock.jointMatrix[i] = jointMat;
			}
			mesh->uniformBlock.jointcount = (float)skin->joints.size();
			memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock, sizeof(mesh->uniformBlock));
		}
		else {
			memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
		}
	}

	for (auto& child : children) {
		child->update();
	}
}

Node::~Node()
{
	if (mesh) {
		delete mesh;
	}
	for (auto& child : children) {
		delete child;
	}
}

/*
glTF default vertex layout with easy Vulkan mapping functions
*/
vk::VertexInputBindingDescription vkmglTF::Vertex::vertexInputBindingDescription;
std::vector<vk::VertexInputAttributeDescription> vkmglTF::Vertex::vertexInputAttributeDescriptions;
vk::PipelineVertexInputStateCreateInfo vkmglTF::Vertex::pipelineVertexInputStateCreateInfo;

vk::VertexInputBindingDescription Vertex::inputBindingDescription(uint32_t binding)
{
	return vk::VertexInputBindingDescription({ binding, sizeof(Vertex), vk::VertexInputRate::eVertex });
}

vk::VertexInputAttributeDescription Vertex::inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component)
{
	switch (component) {
	case VertexComponent::Position:
		return vk::VertexInputAttributeDescription({ location, binding, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos) });
	case VertexComponent::Normal:
		return vk::VertexInputAttributeDescription({ location, binding, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal) });
	case VertexComponent::UV:
		return vk::VertexInputAttributeDescription({ location, binding, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv) });
	case VertexComponent::Color:
		return vk::VertexInputAttributeDescription({ location, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, color) });
	case VertexComponent::Tangent:
		return vk::VertexInputAttributeDescription({ location, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, tangent) });
	case VertexComponent::Joint0:
		return vk::VertexInputAttributeDescription({ location, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, joint0) });
	case VertexComponent::Weight0:
		return vk::VertexInputAttributeDescription({ location, binding, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, weight0) });
	default:
		return vk::VertexInputAttributeDescription({});
	}
}

std::vector<vk::VertexInputAttributeDescription> Vertex::inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components)
{
	std::vector<vk::VertexInputAttributeDescription> result;
	uint32_t location = 0;
	for (VertexComponent component : components) {
		result.push_back(Vertex::inputAttributeDescription(binding, location, component));
		location++;
	}
	return result;
}

vk::PipelineVertexInputStateCreateInfo* Vertex::getPipelineVertexInputState(const std::vector<VertexComponent> components)
{
	vertexInputBindingDescription = Vertex::inputBindingDescription(0);
	Vertex::vertexInputAttributeDescriptions = Vertex::inputAttributeDescriptions(0, components);
	pipelineVertexInputStateCreateInfo.setVertexBindingDescriptions(Vertex::vertexInputBindingDescription)
		.setVertexAttributeDescriptions(Vertex::vertexInputAttributeDescriptions);
		
	return &pipelineVertexInputStateCreateInfo;
}

Texture* Model::getTexture(uint32_t index)
{
	if (index < textures.size()) {
		return &textures[index];
	}
	return nullptr;
}

void Model::createEmptyTexture(vk::Queue transferQueue)
{
	emptyTexture = Texture(device);
	emptyTexture.width = 1;
	emptyTexture.height = 1;
	emptyTexture.layerCount = 1;
	emptyTexture.mipLevels = 1;

	size_t bufferSize = emptyTexture.width * emptyTexture.height * 4;
	unsigned char* buffer = new unsigned char[bufferSize];
	memset(buffer, 0, bufferSize);

	vk::Buffer stagingBuffer;
	vk::DeviceMemory stagingMemory;
	//alloc and bind staging bufferMemory
	device->createBuffer(
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
		bufferSize,
		&stagingBuffer,
		&stagingMemory,
		buffer
	);
	vk::ImageCreateInfo imageCreateInfo;
	emptyTexture.initImageCreateInfo(imageCreateInfo, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
	VK_CHECK_RESULT(device->logicalDevice.createImage(&imageCreateInfo, nullptr, &emptyTexture.image));
	//alloc and bind imageMemory
	emptyTexture.allocImageDeviceMem();

	//
	vk::CommandBuffer copyCmd = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
	vk::BufferImageCopy bufferCopyRegion;
	vk::ImageSubresourceLayers Layers;
	Layers.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setLayerCount(1);
	bufferCopyRegion.setImageSubresource(Layers)
		.setImageExtent({ emptyTexture.width,emptyTexture.height,1 });
	vk::ImageSubresourceRange subresourceRange;
	vkm::initializers::createImageSubresourceRange(subresourceRange, 0, 1, 0, 1);
	vkm::tools::setImageLayout(copyCmd, emptyTexture.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, subresourceRange);
	copyCmd.copyBufferToImage(stagingBuffer, emptyTexture.image, vk::ImageLayout::eTransferDstOptimal, 1, &bufferCopyRegion);
	vkm::tools::setImageLayout(copyCmd, emptyTexture.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, subresourceRange);
	device->flushCommandBuffer(copyCmd, transferQueue);
	emptyTexture.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	// Clean up staging resources
	device->logicalDevice.destroyBuffer(stagingBuffer, nullptr);
	device->logicalDevice.freeMemory(stagingMemory, nullptr);

	// sampler
	emptyTexture.CreateDefaultSampler();
	emptyTexture.CreateImageview(subresourceRange, vk::Format::eR8G8B8A8Unorm);
	emptyTexture.updateDescriptor();
}

Node* Model::findNode(Node* parent, uint32_t index)
{
	Node* nodeFound = nullptr;
	if (parent->index == index) {
		return parent;
	}
	for (auto& child : parent->children) {
		nodeFound = findNode(child, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

Node* Model::nodeFromIndex(uint32_t index)
{
	Node* nodeFound = nullptr;
	for (auto& node : nodes) {
		nodeFound = findNode(node, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

Model::~Model()
{
	device->logicalDevice.destroyBuffer(vertices.buffer);
	device->logicalDevice.freeMemory(vertices.memory);
	device->logicalDevice.destroyBuffer(indices.buffer);
	device->logicalDevice.freeMemory(indices.memory);
	for (auto& texture : textures) {
		texture.destroy();
	}
	for (auto& node : nodes) {
		delete node;
	}
	for (auto& skin : skins) {
		delete skin;
	}
	if (descriptorSetLayoutUbo != VK_NULL_HANDLE) {
		device->logicalDevice.destroyDescriptorSetLayout(descriptorSetLayoutUbo);
		descriptorSetLayoutUbo = VK_NULL_HANDLE;
	}
	if (descriptorSetLayoutImage != VK_NULL_HANDLE) {
		device->logicalDevice.destroyDescriptorSetLayout(descriptorSetLayoutImage);
		descriptorSetLayoutImage = VK_NULL_HANDLE;
	}
	device->logicalDevice.destroyDescriptorPool(descriptorPool);
	emptyTexture.destroy();
}

void Model::loadNode(vkmglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale)
{
	vkmglTF::Node* newNode = new Node{};
	newNode->index = nodeIndex;
	newNode->parent = parent;
	newNode->name = node.name;
	newNode->skinIndex = node.skin;
	newNode->matrix = glm::mat4(1.0f);

	// Generate local node matrix
	glm::vec3 translation = glm::vec3(0.0f);
	if (node.translation.size() == 3) {
		translation = glm::make_vec3(node.translation.data());
		newNode->translation = translation;
	}
	glm::mat4 rotation = glm::mat4(1.0f);
	if (node.rotation.size() == 4) {
		glm::quat q = glm::make_quat(node.rotation.data());
		newNode->rotation = glm::mat4(q);
	}
	glm::vec3 scale = glm::vec3(1.0f);
	if (node.scale.size() == 3) {
		scale = glm::make_vec3(node.scale.data());
		newNode->scale = scale;
	}
	if (node.matrix.size() == 16) {
		newNode->matrix = glm::make_mat4x4(node.matrix.data());
		if (globalscale != 1.0f) {
			newNode->matrix = glm::scale(newNode->matrix, glm::vec3(globalscale));
		}
	}

	// Node with children
	if (node.children.size() > 0) {
		for (auto i = 0; i < node.children.size(); i++) {
			loadNode(newNode, model.nodes[node.children[i]], node.children[i], model, indexBuffer, vertexBuffer, globalscale);
		}
	}

	// Node contains mesh data
	if (node.mesh > -1) {
		const tinygltf::Mesh mesh = model.meshes[node.mesh];
		Mesh* newMesh = new Mesh(device, newNode->matrix);
		newMesh->name = mesh.name;
		for (size_t j = 0; j < mesh.primitives.size(); j++) {
			const tinygltf::Primitive& primitive = mesh.primitives[j];
			if (primitive.indices < 0) {
				continue;
			}
			uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			glm::vec3 posMin{};
			glm::vec3 posMax{};
			bool hasSkin = false;
			// Vertices
			{
				const float* bufferPos = nullptr;
				const float* bufferNormals = nullptr;
				const float* bufferTexCoords = nullptr;
				const float* bufferColors = nullptr;
				const float* bufferTangents = nullptr;
				uint32_t numColorComponents;
				const uint16_t* bufferJoints = nullptr;
				const float* bufferWeights = nullptr;

				// Position attribute is required
				assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

				const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
				bufferPos = reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
				posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
				posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

				if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
					const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
					bufferNormals = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
				}

				if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
					bufferTexCoords = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
				{
					const tinygltf::Accessor& colorAccessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
					const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
					// Color buffer are either of type vec3 or vec4
					numColorComponents = colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
					bufferColors = reinterpret_cast<const float*>(&(model.buffers[colorView.buffer].data[colorAccessor.byteOffset + colorView.byteOffset]));
				}

				if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
				{
					const tinygltf::Accessor& tangentAccessor = model.accessors[primitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView& tangentView = model.bufferViews[tangentAccessor.bufferView];
					bufferTangents = reinterpret_cast<const float*>(&(model.buffers[tangentView.buffer].data[tangentAccessor.byteOffset + tangentView.byteOffset]));
				}

				// Skinning
				// Joints
				if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
					const tinygltf::BufferView& jointView = model.bufferViews[jointAccessor.bufferView];
					bufferJoints = reinterpret_cast<const uint16_t*>(&(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]));
				}

				if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
					const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
					bufferWeights = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				hasSkin = (bufferJoints && bufferWeights);

				vertexCount = static_cast<uint32_t>(posAccessor.count);

				for (size_t v = 0; v < posAccessor.count; v++) {
					Vertex vert{};
					vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
					vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
					vert.uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
					if (bufferColors) {
						switch (numColorComponents) {
						case 3:
							vert.color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]), 1.0f);
							break;
						case 4:
							vert.color = glm::make_vec4(&bufferColors[v * 4]);
							break;
						}
					}
					else {
						vert.color = glm::vec4(1.0f);
					}
					vert.tangent = bufferTangents ? glm::vec4(glm::make_vec4(&bufferTangents[v * 4])) : glm::vec4(0.0f);
					vert.joint0 = hasSkin ? glm::vec4(glm::make_vec4(&bufferJoints[v * 4])) : glm::vec4(0.0f);
					vert.weight0 = hasSkin ? glm::make_vec4(&bufferWeights[v * 4]) : glm::vec4(0.0f);
					vertexBuffer.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
				const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

				indexCount = static_cast<uint32_t>(accessor.count);

				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					uint32_t* buf = new uint32_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					uint16_t* buf = new uint16_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					uint8_t* buf = new uint8_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				default:
					std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
					return;
				}
			}
			Primitive* newPrimitive = new Primitive(indexStart, indexCount, primitive.material > -1 ? materials[primitive.material] : materials.back());
			newPrimitive->firstVertex = vertexStart;
			newPrimitive->vertexCount = vertexCount;
			newPrimitive->setDimensions(posMin, posMax);
			newMesh->primitives.push_back(newPrimitive);
		}
		newNode->mesh = newMesh;
	}
	if (parent) {
		parent->children.push_back(newNode);
	}
	else {
		nodes.push_back(newNode);
	}
	linearNodes.push_back(newNode);
}

void Model::loadSkins(tinygltf::Model& gltfModel)
{
	for (tinygltf::Skin& source : gltfModel.skins) {
		Skin* newSkin = new Skin{};
		newSkin->name = source.name;

		// Find skeleton root node
		if (source.skeleton > -1) {
			newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
		}

		// Find joint nodes
		for (int jointIndex : source.joints) {
			Node* node = nodeFromIndex(jointIndex);
			if (node) {
				newSkin->joints.push_back(nodeFromIndex(jointIndex));
			}
		}

		// Get inverse bind matrices from buffer
		if (source.inverseBindMatrices > -1) {
			const tinygltf::Accessor& accessor = gltfModel.accessors[source.inverseBindMatrices];
			const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
			newSkin->inverseBindMatrices.resize(accessor.count);
			memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
		}

		skins.push_back(newSkin);
	}
}

void Model::loadImages(tinygltf::Model& gltfModel, vk::Queue transferQueue)
{
	for (tinygltf::Image& image : gltfModel.images) {
		Texture texture(device);
		texture.fromglTFimage(image, path, transferQueue);
		texture.index = static_cast<uint32_t>(textures.size());
		textures.push_back(texture);
	}
	// Create an empty texture to be used for empty material images
	createEmptyTexture(transferQueue);
}
void Model::loadMaterials(tinygltf::Model& gltfModel)
{
	for (tinygltf::Material& mat : gltfModel.materials) {
		vkmglTF::Material material(device);
		if (mat.values.find("baseColorTexture") != mat.values.end()) {
			material.baseColorTexture = getTexture(gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source);
		}
		// Metallic roughness workflow
		if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
			material.metallicRoughnessTexture = getTexture(gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source);
		}
		if (mat.values.find("roughnessFactor") != mat.values.end()) {
			material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
		}
		if (mat.values.find("metallicFactor") != mat.values.end()) {
			material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
		}
		if (mat.values.find("baseColorFactor") != mat.values.end()) {
			material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
		}
		if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
			material.normalTexture = getTexture(gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source);
		}
		else {
			material.normalTexture = &emptyTexture;
		}
		if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
			material.emissiveTexture = getTexture(gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
			material.occlusionTexture = getTexture(gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
			tinygltf::Parameter param = mat.additionalValues["alphaMode"];
			if (param.string_value == "BLEND") {
				material.alphaMode = Material::ALPHAMODE_BLEND;
			}
			if (param.string_value == "MASK") {
				material.alphaMode = Material::ALPHAMODE_MASK;
			}
		}
		if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
			material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
		}

		materials.push_back(material);
	}
	// Push a default material at the end of the list for meshes with no material assigned
	materials.push_back(Material(device));
}

void Model::loadFromFile(std::string filename, vk::Queue transferQueue, uint32_t fileLoadingFlags, float scale)
{
	assert(device);
	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF gltfContext;
	if (fileLoadingFlags & FileLoadingFlags::DontLoadImages) {
		gltfContext.SetImageLoader(loadImageDataFuncEmpty, nullptr);
	}
	else {
		gltfContext.SetImageLoader(loadImageDataFunc, nullptr);
	}

	size_t pos = filename.find_last_of('/');
	path = filename.substr(0, pos);

	std::string error, warning;
	bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename);

	std::vector<uint32_t> indexBuffer;
	std::vector<Vertex> vertexBuffer;

	if (fileLoaded) {
		if (!(fileLoadingFlags & FileLoadingFlags::DontLoadImages)) {
			loadImages(gltfModel, transferQueue);
		}
		loadMaterials(gltfModel);
		const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
		for (size_t i = 0; i < scene.nodes.size(); i++) {
			const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
			loadNode(nullptr, node, scene.nodes[i], gltfModel, indexBuffer, vertexBuffer, scale);
		}
		//if (gltfModel.animations.size() > 0) {
		//	loadAnimations(gltfModel);
		//}
		loadSkins(gltfModel);

		for (auto node : linearNodes) {
			// Assign skins
			if (node->skinIndex > -1) {
				node->skin = skins[node->skinIndex];
			}
			// Initial pose
			if (node->mesh) {
				node->update();
			}
		}
	}
	else {
		vkm::tools::exitFatal("Could not load glTF file \"" + filename + "\": " + error, -1);
		return;
	}

	// Pre-Calculations for requested features
	PreCalculations(fileLoadingFlags, vertexBuffer);

	for (auto& extension : gltfModel.extensionsUsed) {
		if (extension == "KHR_materials_pbrSpecularGlossiness") {
			std::cout << "Required extension: " << extension;
		}
	}

	size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
	size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
	indices.count = static_cast<uint32_t>(indexBuffer.size());
	vertices.count = static_cast<uint32_t>(vertexBuffer.size());
	assert((vertexBufferSize > 0) && (indexBufferSize > 0));

	struct StagingBuffer {
		vk::Buffer buffer;
		vk::DeviceMemory memory;
	} vertexStaging{}, indexStaging{};
	// Create staging buffers |Vertex data
	VK_CHECK_RESULT(device->createBuffer(
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
		vertexBufferSize,
		&vertexStaging.buffer,
		&vertexStaging.memory,
		vertexBuffer.data())
	);
	// Index data
	VK_CHECK_RESULT(device->createBuffer(
		vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
		indexBufferSize,
		&indexStaging.buffer,
		&indexStaging.memory,
		indexBuffer.data())
	);
	//Create device local buffers
	VK_CHECK_RESULT(device->createBuffer(
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
		vk::MemoryPropertyFlagBits::eDeviceLocal | memoryPropertyFlags,
		vertexBufferSize,
		&vertices.buffer,
		&vertices.memory)
	);
	// Index data
	VK_CHECK_RESULT(device->createBuffer(
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
		vk::MemoryPropertyFlagBits::eDeviceLocal | memoryPropertyFlags,			//eDeviceLocal can't map
		indexBufferSize,
		&indices.buffer,
		&indices.memory)
	);

	vk::CommandBuffer copyCmd = device->createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
	vk::BufferCopy copyRegion = {};
	copyRegion.setSize(vertexBufferSize);
	copyCmd.copyBuffer(vertexStaging.buffer, vertices.buffer, copyRegion);
	copyRegion.setSize(indexBufferSize);
	copyCmd.copyBuffer(indexStaging.buffer, indices.buffer, copyRegion);

	device->flushCommandBuffer(copyCmd, transferQueue, true/*free copyCmd*/);
	device->logicalDevice.destroyBuffer( vertexStaging.buffer, nullptr);
	device->logicalDevice.freeMemory( vertexStaging.memory, nullptr);
	device->logicalDevice.destroyBuffer( indexStaging.buffer, nullptr);
	device->logicalDevice.freeMemory(indexStaging.memory, nullptr);

	getSceneDimensions();

	// Setup descriptors
	uint32_t uboCount = 0;
	uint32_t imageCount = 0;
	for (auto& node : linearNodes) {
		if (node->mesh) {
			uboCount++;
		}
	}
	for (auto& material : materials) {
		if (material.baseColorTexture != nullptr) {
			imageCount++;
		}
	}

	std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer,uboCount } };
	if (imageCount > 0) {
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
			poolSizes.push_back({ vk::DescriptorType::eCombinedImageSampler, imageCount });
		}
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
			poolSizes.push_back({ vk::DescriptorType::eCombinedImageSampler, imageCount });
		}
	}

	vk::DescriptorPoolCreateInfo descrptoPoolCreateInfo;
	descrptoPoolCreateInfo.setMaxSets(uboCount + imageCount)
		.setPoolSizes(poolSizes);
	VK_CHECK_RESULT(device->logicalDevice.createDescriptorPool(&descrptoPoolCreateInfo, nullptr, &descriptorPool));
	// Descriptors for per-node uniform buffers
	{
		if (descriptorSetLayoutUbo == VK_NULL_HANDLE) {
			vk::DescriptorSetLayoutBinding setLayoutBinding={ /*binding */0, /*descriptorType*/ vk::DescriptorType::eUniformBuffer, /*descriptorCount*/ 1, /*stageFlags*/  vk::ShaderStageFlagBits::eVertex };
			vk::DescriptorSetLayoutCreateInfo descriptorLayoutCI{ {},/*bindingCount*/ 1,/* pBindings*/  &setLayoutBinding };
			VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout(&descriptorLayoutCI, nullptr, &descriptorSetLayoutUbo));
		}
		//updateDes
		for (auto node : nodes) {
			updateNodeDescriptorSets(node, descriptorSetLayoutUbo);
		}
	}
	// Descriptors for per-material images
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutImage == VK_NULL_HANDLE) {
			std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{};
			if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
				setLayoutBindings.push_back({ /*.binding = */static_cast<uint32_t>(setLayoutBindings.size()), /*.descriptorType =*/ vk::DescriptorType::eCombinedImageSampler, /*.descriptorCount = */1, /*.stageFlags =*/ vk::ShaderStageFlagBits::eFragment });
			}
			if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
				setLayoutBindings.push_back({ static_cast<uint32_t>(setLayoutBindings.size()), vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment });
			}
			vk::DescriptorSetLayoutCreateInfo descriptorLayoutCI;
			descriptorLayoutCI.setBindingCount(1)
				.setPBindings(setLayoutBindings.data());
			VK_CHECK_RESULT(device->logicalDevice.createDescriptorSetLayout( &descriptorLayoutCI, nullptr, &descriptorSetLayoutImage));
		}
		for (auto& material : materials) {
			if (material.baseColorTexture != nullptr) {
				material.createDescriptorSet(descriptorPool, vkmglTF::descriptorSetLayoutImage, descriptorBindingFlags);
			}
		}
	}
}

void Model::bindBuffers(vk::CommandBuffer commandBuffer)
{
	const vk::DeviceSize offsets[1] = { 0 };
	commandBuffer.bindVertexBuffers(0, 1, &vertices.buffer, offsets);
	commandBuffer.bindIndexBuffer(indices.buffer, 0, vk::IndexType::eUint32);
	buffersBound = true;
}

void Model::drawNode(Node* node, vk::CommandBuffer commandBuffer, uint32_t renderFlags, vk::PipelineLayout pipelineLayout, uint32_t bindImageSet)
{
	if (node->mesh) {
		for (Primitive* primitive : node->mesh->primitives) {
			bool skip = false;
			const vkmglTF::Material& material = primitive->material;
			if (renderFlags & RenderFlags::RenderOpaqueNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_OPAQUE);
			}
			if (renderFlags & RenderFlags::RenderAlphaMaskedNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_MASK);
			}
			if (renderFlags & RenderFlags::RenderAlphaBlendedNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_BLEND);
			}
			if (!skip) {
				if (renderFlags & RenderFlags::BindImages) {
					commandBuffer.bindDescriptorSets( vk::PipelineBindPoint::eGraphics, pipelineLayout, bindImageSet, 1, &material.descriptorSet, 0, nullptr);
				}
				commandBuffer.drawIndexed(primitive->indexCount, 1, primitive->firstIndex, 0, 0);
			}
		}
	}
	for (auto& child : node->children) {
		drawNode(child, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
	}
}

void Model::draw(vk::CommandBuffer commandBuffer, uint32_t renderFlags, vk::PipelineLayout pipelineLayout, uint32_t bindImageSet)
{
	if (!buffersBound) {
		const vk::DeviceSize offsets[1] = { 0 };
		commandBuffer.bindVertexBuffers(0, 1, &vertices.buffer, offsets);
		commandBuffer.bindIndexBuffer(indices.buffer, 0, vk::IndexType::eUint32);
	}
	for (auto& node : nodes) {
		drawNode(node, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
	}
}

void Model::updateNodeDescriptorSets(Node* node, vk::DescriptorSetLayout descriptorSetLayout)
{
	if (node->mesh) {
		vk::DescriptorSetAllocateInfo descriptorSetAllocInfo;
		descriptorSetAllocInfo.setDescriptorPool(descriptorPool)
			.setSetLayouts(descriptorSetLayout);

		VK_CHECK_RESULT(device->logicalDevice.allocateDescriptorSets(&descriptorSetAllocInfo, &node->mesh->uniformBuffer.descriptorSet));
		vk::WriteDescriptorSet writeDescriptorSet;
		writeDescriptorSet.setDstSet(node->mesh->uniformBuffer.descriptorSet)
			.setDstBinding(0)
			.setDescriptorCount(1)
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setPBufferInfo(&node->mesh->uniformBuffer.descriptor);
		device->logicalDevice.updateDescriptorSets(writeDescriptorSet, {});
	}
	for (auto& child : node->children) {
		updateNodeDescriptorSets(child, descriptorSetLayout);
	}
}

void Model::PreCalculations(uint32_t fileLoadingFlags, std::vector<vkmglTF::Vertex>& vertexBuffer)
{
	if ((fileLoadingFlags & FileLoadingFlags::PreTransformVertices) || (fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors) || (fileLoadingFlags & FileLoadingFlags::FlipY)) {
		const bool preTransform = fileLoadingFlags & FileLoadingFlags::PreTransformVertices;
		const bool preMultiplyColor = fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors;
		const bool flipY = fileLoadingFlags & FileLoadingFlags::FlipY;
		for (Node* node : linearNodes) {
			if (node->mesh) {
				const glm::mat4 localMatrix = node->getMatrix();
				for (Primitive* primitive : node->mesh->primitives) {
					for (uint32_t i = 0; i < primitive->vertexCount; i++) {
						Vertex& vertex = vertexBuffer[primitive->firstVertex + i];
						// Pre-transform vertex positions by node-hierarchy
						if (preTransform) {
							vertex.pos = glm::vec3(localMatrix * glm::vec4(vertex.pos, 1.0f));
							vertex.normal = glm::normalize(glm::mat3(localMatrix) * vertex.normal);
						}
						// Flip Y-Axis of vertex positions
						if (flipY) {
							vertex.pos.y *= -1.0f;
							vertex.normal.y *= -1.0f;
						}
						// Pre-Multiply vertex colors with material base color
						if (preMultiplyColor) {
							vertex.color = primitive->material.baseColorFactor * vertex.color;
						}
					}
				}
			}
		}
	}
}

void Model::getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max)
{
	if (node->mesh) {
		for (Primitive* primitive : node->mesh->primitives) {
			glm::vec4 locMin = glm::vec4(primitive->dimensions.min, 1.0f) * node->getMatrix();
			glm::vec4 locMax = glm::vec4(primitive->dimensions.max, 1.0f) * node->getMatrix();
			if (locMin.x < min.x) { min.x = locMin.x; }
			if (locMin.y < min.y) { min.y = locMin.y; }
			if (locMin.z < min.z) { min.z = locMin.z; }
			if (locMax.x > max.x) { max.x = locMax.x; }
			if (locMax.y > max.y) { max.y = locMax.y; }
			if (locMax.z > max.z) { max.z = locMax.z; }
		}
	}
	for (auto child : node->children) {
		getNodeDimensions(child, min, max);
	}
}

void Model::getSceneDimensions()
{
	dimensions.min = glm::vec3(FLT_MAX);
	dimensions.max = glm::vec3(-FLT_MAX);
	for (auto node : nodes) {
		getNodeDimensions(node, dimensions.min, dimensions.max);
	}
	dimensions.size = dimensions.max - dimensions.min;
	dimensions.center = (dimensions.min + dimensions.max) / 2.0f;
	dimensions.radius = glm::distance(dimensions.min, dimensions.max) / 2.0f;
}

}
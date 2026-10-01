#include "application.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <cmath>
#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace application {
namespace {

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 normalMatrix;
    alignas(16) glm::vec4 tintColor;
};

constexpr size_t OBJECT_COUNT = 2;

VkBuffer vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vertex_buffer_alloc = VK_NULL_HANDLE;
VkBuffer index_buffer = VK_NULL_HANDLE;
VmaAllocation index_buffer_alloc = VK_NULL_HANDLE;
uint32_t index_count = 0;

VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet descriptor_sets[OBJECT_COUNT] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

VkBuffer ubo_buffers[OBJECT_COUNT] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
VmaAllocation ubo_allocations[OBJECT_COUNT] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
void* ubo_mapped_ptrs[OBJECT_COUNT] = { nullptr, nullptr };

VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline graphics_pipeline = VK_NULL_HANDLE;

bool use_perspective = true;
float fov = 45.0f;
float ortho_size = 3.5f;
float near_plane = 0.1f;
float far_plane = 100.0f;
glm::vec3 camera_pos = glm::vec3(0.0f, 2.5f, 6.0f);
glm::vec3 camera_target = glm::vec3(0.0f, 0.0f, 0.0f);

glm::vec3 obj1_pos = glm::vec3(0.0f, 0.0f, 0.0f);
glm::vec3 obj1_rot = glm::vec3(0.0f, 0.0f, 0.0f); 
glm::vec3 obj1_scale = glm::vec3(1.0f, 1.0f, 1.0f);
glm::vec4 obj1_tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

bool anim_playing = true;
float anim_speed = 1.0f;
double anim_time = 0.0;
double last_time = 0.0;
bool use_trajectory = true;
float traj_radius = 2.0f;

bool obj2_enabled = true;
glm::vec3 obj2_pos = glm::vec3(2.5f, 0.0f, 0.0f);
glm::vec3 obj2_scale = glm::vec3(0.5f, 0.5f, 0.5f);
glm::vec4 obj2_tint = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f);

std::vector<char> readShaderFile(const std::string& filename) {
    std::vector<std::string> search_paths = { filename, "shaders/" + filename, "../shaders/" + filename };
    for (const auto& path : search_paths) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (file.is_open()) {
            size_t fileSize = (size_t)file.tellg();
            std::vector<char> buffer(fileSize);
            file.seekg(0);
            file.read(buffer.data(), fileSize);
            file.close();
            return buffer;
        }
    }
    std::cerr << "Failed to find shader: " << filename << '\n';
    return {};
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data()),
    };
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    return shaderModule;
}

void generateSphere(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
    const uint32_t stacks = 10;
    const uint32_t slices = 12;
    const float radius = 1.0f;

    vertices.push_back({
        glm::vec3(0.0f, radius, 0.0f),
        glm::vec3(0.5f, 1.0f, 0.5f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    });

    for (uint32_t i = 1; i < stacks; ++i) {
        float phi = glm::pi<float>() * float(i) / float(stacks);
        float y = radius * std::cos(phi);
        float r_sin_phi = radius * std::sin(phi);
        
        for (uint32_t j = 0; j < slices; ++j) {
            float theta = 2.0f * glm::pi<float>() * float(j) / float(slices);
            float x = r_sin_phi * std::cos(theta);
            float z = r_sin_phi * std::sin(theta);
            
            glm::vec3 pos(x, y, z);
            glm::vec3 norm = glm::normalize(pos);
            glm::vec3 col = (norm + glm::vec3(1.0f)) * 0.5f;
            
            vertices.push_back({ pos, col, norm });
        }
    }

    vertices.push_back({
        glm::vec3(0.0f, -radius, 0.0f),
        glm::vec3(0.5f, 0.0f, 0.5f),
        glm::vec3(0.0f, -1.0f, 0.0f)
    });

    for (uint32_t j = 0; j < slices; ++j) {
        uint16_t next = (j + 1) % slices;
        indices.push_back(0);
        indices.push_back(1 + j);
        indices.push_back(1 + next);
    }

    for (uint32_t i = 0; i < stacks - 2; ++i) {
        uint16_t ring1 = 1 + i * slices;
        uint16_t ring2 = 1 + (i + 1) * slices;
        for (uint32_t j = 0; j < slices; ++j) {
            uint16_t next = (j + 1) % slices;
            indices.push_back(ring1 + j);
            indices.push_back(ring2 + j);
            indices.push_back(ring2 + next);

            indices.push_back(ring1 + j);
            indices.push_back(ring2 + next);
            indices.push_back(ring1 + next);
        }
    }
    uint16_t south_pole_idx = static_cast<uint16_t>(vertices.size() - 1);
    uint16_t last_ring = 1 + (stacks - 2) * slices;
    for (uint32_t j = 0; j < slices; ++j) {
        uint16_t next = (j + 1) % slices;
        indices.push_back(south_pole_idx);
        indices.push_back(last_ring + next);
        indices.push_back(last_ring + j);
    }
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkBuffer& buffer, VmaAllocation& allocation, void** mapped_ptr = nullptr) {
    auto& context = graphics::internal::context;
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo allocInfo = { .usage = VMA_MEMORY_USAGE_CPU_TO_GPU };
    
    if (vmaCreateBuffer(context.allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    if (mapped_ptr) vmaMapMemory(context.allocator, allocation, mapped_ptr);
    return true;
}


bool initialize() {
    auto& context = graphics::internal::context;

    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    generateSphere(vertices, indices);
    index_count = static_cast<uint32_t>(indices.size());

    void* v_data = nullptr;
    createBuffer(sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer, vertex_buffer_alloc, &v_data);
    std::memcpy(v_data, vertices.data(), sizeof(Vertex) * vertices.size());
    vmaUnmapMemory(context.allocator, vertex_buffer_alloc);

    void* i_data = nullptr;
    createBuffer(sizeof(uint16_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer, index_buffer_alloc, &i_data);
    std::memcpy(i_data, indices.data(), sizeof(uint16_t) * indices.size());
    vmaUnmapMemory(context.allocator, index_buffer_alloc);

    for (size_t i = 0; i < OBJECT_COUNT; ++i) {
        createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ubo_buffers[i], ubo_allocations[i], &ubo_mapped_ptrs[i]);
    }

    VkDescriptorSetLayoutBinding ubo_layout_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &ubo_layout_binding,
    };
    vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &descriptor_set_layout);

    VkDescriptorPoolSize pool_size = { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = OBJECT_COUNT };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = OBJECT_COUNT,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool);

    std::vector<VkDescriptorSetLayout> layouts(OBJECT_COUNT, descriptor_set_layout);
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = OBJECT_COUNT,
        .pSetLayouts = layouts.data(),
    };
    vkAllocateDescriptorSets(context.device, &alloc_info, descriptor_sets);

    for (size_t i = 0; i < OBJECT_COUNT; ++i) {
        VkDescriptorBufferInfo buffer_info = { .buffer = ubo_buffers[i], .offset = 0, .range = sizeof(UniformBufferObject) };
        VkWriteDescriptorSet descriptor_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets[i],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buffer_info,
        };
        vkUpdateDescriptorSets(context.device, 1, &descriptor_write, 0, nullptr);
    }

    auto vert_code = readShaderFile("shader.vert.spv");
    auto frag_code = readShaderFile("shader.frag.spv");
    VkShaderModule vert_module = createShaderModule(context.device, vert_code);
    VkShaderModule frag_module = createShaderModule(context.device, frag_code);

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" }
    };

    VkVertexInputBindingDescription binding_desc = { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attribute_descs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding_desc,
        .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attribute_descs,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo viewport_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = { .blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo color_blending = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color_blend_attachment };
    
    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamic_states };
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &descriptor_set_layout };
    vkCreatePipelineLayout(context.device, &pipeline_layout_info, nullptr, &pipeline_layout);

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = shader_stages,
        .pVertexInputState = &vertex_input_info, .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state, .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling, .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending, .pDynamicState = &dynamic_state_info,
        .layout = pipeline_layout, .renderPass = context.render_pass,
    };
    vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline);

    vkDestroyShaderModule(context.device, frag_module, nullptr);
    vkDestroyShaderModule(context.device, vert_module, nullptr);
    return true;
}

void shutdown() {
    auto& context = graphics::internal::context;
    vkQueueWaitIdle(context.graphics_queue);
    vkDestroyPipeline(context.device, graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
    vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
    for (size_t i = 0; i < OBJECT_COUNT; ++i) {
        vmaUnmapMemory(context.allocator, ubo_allocations[i]);
        vmaDestroyBuffer(context.allocator, ubo_buffers[i], ubo_allocations[i]);
    }
    vmaDestroyBuffer(context.allocator, index_buffer, index_buffer_alloc);
    vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_buffer_alloc);
}

void update(double time) {
    if (last_time == 0.0) last_time = time;
    float dt = static_cast<float>(time - last_time);
    last_time = time;
    if (anim_playing) anim_time += dt * anim_speed;

    ImGui::Begin("Lab 1: Variant 5 (Sphere)");
    
    ImGui::Checkbox("Perspective Projection", &use_perspective);
    if (use_perspective) ImGui::SliderFloat("FOV", &fov, 20.0f, 120.0f);
    else ImGui::SliderFloat("Ortho Size", &ortho_size, 0.5f, 10.0f);
    ImGui::DragFloat3("Camera Pos", glm::value_ptr(camera_pos), 0.1f);

    ImGui::Separator();
    ImGui::DragFloat3("Obj 1 Position", glm::value_ptr(obj1_pos), 0.05f);
    ImGui::SliderFloat3("Obj 1 Rotation", glm::value_ptr(obj1_rot), -180.0f, 180.0f);
    ImGui::DragFloat3("Obj 1 Scale", glm::value_ptr(obj1_scale), 0.05f, 0.1f, 5.0f);
    ImGui::ColorEdit4("Obj 1 Tint", glm::value_ptr(obj1_tint));

    ImGui::Checkbox("Animate along Trefoil Knot", &use_trajectory);
    if (use_trajectory) {
        ImGui::Checkbox("Play Animation", &anim_playing);
        ImGui::SliderFloat("Speed", &anim_speed, 0.0f, 3.0f);
        ImGui::SliderFloat("Radius", &traj_radius, 0.5f, 5.0f);
    }

    ImGui::Separator();
    ImGui::Checkbox("Render Object 2", &obj2_enabled);
    ImGui::ColorEdit4("Obj 2 Tint", glm::value_ptr(obj2_tint));
    ImGui::End();

    auto& context = graphics::internal::context;
    float aspect = (float)context.swapchain_extent.width / (float)context.swapchain_extent.height;

    glm::mat4 proj = use_perspective 
        ? glm::perspective(glm::radians(fov), aspect, near_plane, far_plane)
        : glm::ortho(-ortho_size * aspect, ortho_size * aspect, -ortho_size, ortho_size, near_plane, far_plane);
    
    proj[1][1] *= -1.0f; 
    glm::mat4 view = glm::lookAt(camera_pos, camera_target, glm::vec3(0.0f, 1.0f, 0.0f));

    float t = static_cast<float>(anim_time);
    if (use_trajectory) {
        obj1_pos.x = traj_radius * (std::sin(t) + 2.0f * std::sin(2.0f * t)) * 0.3f;
        obj1_pos.y = traj_radius * (std::cos(t) - 2.0f * std::cos(2.0f * t)) * 0.3f;
        obj1_pos.z = traj_radius * (-std::sin(3.0f * t)) * 0.3f;
        obj1_rot.y += dt * 50.0f;
    }

    glm::mat4 model1 = glm::translate(glm::mat4(1.0f), obj1_pos);
    model1 = glm::rotate(model1, glm::radians(obj1_rot.y), glm::vec3(0, 1, 0));
    model1 = glm::scale(model1, obj1_scale);

    UniformBufferObject ubo1 = {
        .model = model1,
        .view = view,
        .proj = proj,
        .normalMatrix = glm::transpose(glm::inverse(model1)),
        .tintColor = obj1_tint
    };
    std::memcpy(ubo_mapped_ptrs[0], &ubo1, sizeof(ubo1));

    glm::vec3 pos2 = obj2_enabled ? glm::vec3(2.5f * std::cos(t), 0.0f, 2.5f * std::sin(t)) : glm::vec3(0.0f);
    glm::mat4 model2 = glm::translate(glm::mat4(1.0f), pos2);
    model2 = glm::scale(model2, obj2_scale);

    UniformBufferObject ubo2 = {
        .model = model2,
        .view = view,
        .proj = proj,
        .normalMatrix = glm::transpose(glm::inverse(model2)),
        .tintColor = obj2_tint
    };
    std::memcpy(ubo_mapped_ptrs[1], &ubo2, sizeof(ubo2));
}

void render(const graphics::internal::FrameData& fd) {
    auto& context = graphics::internal::context;
    vkResetCommandBuffer(fd.command_buffer, 0);
    VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(fd.command_buffer, &begin_info);

    VkClearValue clear_values[2] = { {{0.1f, 0.1f, 0.15f, 1.0f}}, {1.0f, 0} };
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass, .framebuffer = fd.framebuffer,
        .renderArea = { .extent = context.swapchain_extent },
        .clearValueCount = 2, .pClearValues = clear_values,
    };
    vkCmdBeginRenderPass(fd.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = { 0, 0, (float)context.swapchain_extent.width, (float)context.swapchain_extent.height, 0.0f, 1.0f };
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    VkRect2D scissor = { {0, 0}, context.swapchain_extent };
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, offsets);
    vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_sets[0], 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);

    if (obj2_enabled) {
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_sets[1], 0, nullptr);
        vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
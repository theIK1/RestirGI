#pragma once

#include "stdafx.h"
#include <DirectXMath.h>

using namespace DirectX;
#define MY_TEXTURE_2D_BINDLESS_TABLE_SIZE 200

struct Vertex_Simple
{
    XMFLOAT3 Position;
    XMFLOAT4 Color;
};

struct MeshMaterial
{
    float base_color_mult[4];
    UINT normal_map;
    UINT spec_map;
    UINT albedo_map;
    UINT emissive_map;
    float roughness_mult;
    float metalness_factor;
    float emissive[3];
    UINT flags;
    float map_transforms[6 * 4];

    MeshMaterial() {
        base_color_mult[0] = 1.0;
        base_color_mult[1] = 1.0;
        base_color_mult[2] = 1.0;
        base_color_mult[3] = 1.0;
        normal_map = MY_TEXTURE_2D_BINDLESS_TABLE_SIZE;
        spec_map = MY_TEXTURE_2D_BINDLESS_TABLE_SIZE;
        albedo_map = MY_TEXTURE_2D_BINDLESS_TABLE_SIZE;
        emissive_map = MY_TEXTURE_2D_BINDLESS_TABLE_SIZE;
        roughness_mult = 1.0;
        metalness_factor = 1.0;
        emissive[0] = 0.0;
        emissive[1] = 0.0;
        emissive[2] = 0.0;
        flags = 0;
        for (UINT i = 0; i < 24; ++i)
            map_transforms[i] = 0.0;
    }
};

struct Vertex_Model
{
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT2 TexCoord;
    UINT     MeshID;
    XMFLOAT3 Tangent;
    XMFLOAT3 Bitangent;
    XMFLOAT4 Color;
    UINT MaterialID;
};

static D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"MESHID", 0, DXGI_FORMAT_R32_UINT, 0, 32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,36,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    {"BITANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,60,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    {"MaterialID",0,DXGI_FORMAT_R32_UINT,0,76,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
};

struct Vertex_Light 
{
    XMFLOAT3 Position;
    XMFLOAT2 TexCoord;
};

static D3D12_INPUT_ELEMENT_DESC inputLightElementDescs[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
};

struct Mesh
{
    // Give it a name so we can look it up by name.
    std::string Name;

    // System memory copies.  Use Blobs because the vertex/index format can be generic.
    // It is up to the client to cast appropriately.  
    //Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
    //Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

    // Data about the buffers.
    UINT VertexByteStride = 0;
    UINT VertexBufferByteSize = 0;
    UINT VertexCount = 0;
    DXGI_FORMAT IndexFormat = DXGI_FORMAT_R32_UINT;
    UINT IndexBufferByteSize = 0;
    UINT IndexCount = 0;

    D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const
    {
        D3D12_VERTEX_BUFFER_VIEW vbv;
        vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
        vbv.StrideInBytes = VertexByteStride;
        vbv.SizeInBytes = VertexBufferByteSize;

        return vbv;
    }

    D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
    {
        D3D12_INDEX_BUFFER_VIEW ibv;
        ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
        ibv.Format = IndexFormat;
        ibv.SizeInBytes = IndexBufferByteSize;

        return ibv;
    }

    // We can free this memory after we finish upload to the GPU.
    void DisposeUploaders()
    {
        VertexBufferUploader = nullptr;
        IndexBufferUploader = nullptr;
    }
};

//
struct Texture
{
    std::string FileName;
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadResource = nullptr;

    std::string Type;
    UINT width;
    UINT height;
    //index in Texture loader
    UINT SrvHeapIndex = 0;
};

struct Model
{
    std::string Name;
    std::string Directory;
    //mesh --- textures id :vector[0] - diffuse(albedo) map, vector[1] - specular, vector[2] - normal, vector[3] - emissive
    std::vector<std::pair<std::unique_ptr<Mesh>, std::vector<UINT>>> Meshes;
    std::vector<std::shared_ptr<Texture>> Textures;

    std::vector<int> MaterialID;

    std::vector<MeshMaterial> Mat;
    Microsoft::WRL::ComPtr<ID3D12Resource> MaterialBuffer = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> MaterialBufferUploader = nullptr;

};

struct Material
{
    std::string Name;

    // Material constant buffer data used for shading.
    XMFLOAT3 ambient;
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = .25f;

};







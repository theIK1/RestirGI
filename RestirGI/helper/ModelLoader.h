#pragma once

#ifdef _WIN64
#pragma comment (lib, "assimp-vc140-mt.lib")
#elif defined _WIN32
#pragma comment (lib, "assimp-vc140-mt.lib")
#endif

#include "stdafx.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

struct Model;
struct Mesh;
class TextureLoader;
class ModelLoader
{
public:
	ModelLoader(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,TextureLoader* textureLoader);
	ModelLoader(const ModelLoader&) = delete;
	ModelLoader(const ModelLoader&&) = delete;

	bool Load(std::string filename, Model& model,
		unsigned int loadFlag = aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_ConvertToLeftHanded);//


private:
	// Process Assimp Scene Node and Mesh
	bool ProcessNode(aiNode* ai_node, const aiScene* ai_scene, Model& model);
	bool ProcessMesh(aiMesh* ai_mesh, const aiScene* ai_scene, Model& model);

	ID3D12Device* m_device;
	ID3D12GraphicsCommandList* m_cmdList;
	TextureLoader* m_textureLoader;
	std::string m_textureType;
	std::string m_modelDic;
	int			m_indexInTextureLoader = 0;
	int         CurrentID = 0;
	//测试用，用以统计各种贴图数量
	int countMap[13]{};
	std::vector<std::vector<UINT>> test;
	

	bool LoadMaterialTextures(aiMaterial* ai_mat, aiTextureType ai_texType, std::string typeName, const aiScene* ai_scene, 
		std::vector<std::shared_ptr<Texture>>& textures, UINT& materialIndex);



	std::string DetermineTextureType(const aiScene* ai_scene, aiMaterial* ai_mat);
};


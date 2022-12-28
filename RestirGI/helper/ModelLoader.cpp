#include "stdafx.h"
#include "core/D3DUtility.h"
#include "helper/DXSampleHelper.h"
#include <iostream>
#include "ModelLoader.h"
#include "TextureLoader.h"

ModelLoader::ModelLoader(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,TextureLoader* textureLoader)
	: m_device ( device),m_cmdList (cmdList),m_textureLoader(textureLoader)
{
}

bool ModelLoader::Load(std::string filename, Model& model, unsigned int loadFlag)
{
	Assimp::Importer m_importer;
	const aiScene* pScene = m_importer.ReadFile(filename, loadFlag);
	
	if (!pScene || pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !pScene->mRootNode) {
		std::cout << "ERROR::ASSIMP:: " << m_importer.GetErrorString() << std::endl;
		return false;
	}


	model.Directory = filename.substr(0, filename.find_last_of('/'));
	m_modelDic = model.Directory;

	m_indexInTextureLoader = 0;

	bool result;
	result = ProcessNode(pScene->mRootNode, pScene, model);


	model.MaterialBuffer = helper::CreateDefaultBuffer(m_device, m_cmdList, model.Mat.data(), sizeof(MeshMaterial) * model.Mat.size(), model.MaterialBufferUploader);
	//int count[10]{};
	//for (const auto& p : model.Meshes)
	//{
	//	count[p.second.size()]++;
	//}

	return result;
}



bool ModelLoader::ProcessNode(aiNode* ai_node, const aiScene* ai_scene, Model& model)
{
	
	for (UINT i = 0; i < ai_node->mNumMeshes; i++){
		aiMesh* ai_mesh = ai_scene->mMeshes[ai_node->mMeshes[i]];
		ProcessMesh(ai_mesh, ai_scene, model);
	}

	for (UINT i = 0; i < ai_node->mNumChildren; i++){
		ProcessNode(ai_node->mChildren[i], ai_scene, model);
	}

	return true;
}

bool ModelLoader::ProcessMesh(aiMesh* ai_mesh, const aiScene* ai_scene, Model& model)
{
	std::vector<UINT> materialID;
	// Data to fill
	std::vector<Vertex_Model> vertices;
	std::vector<UINT> indices;
	//std::vector<Texture> textures;

	// Walk through each of the mesh's vertices
	for (UINT i = 0; i < ai_mesh->mNumVertices; i++)
	{
		Vertex_Model vertex;

		vertex.Position.x = ai_mesh->mVertices[i].x ;
		vertex.Position.y = ai_mesh->mVertices[i].y ;
		vertex.Position.z = ai_mesh->mVertices[i].z ;

		if (ai_mesh->HasNormals()){
			vertex.Normal.x = ai_mesh->mNormals[i].x;
			vertex.Normal.y = ai_mesh->mNormals[i].y;
			vertex.Normal.z = ai_mesh->mNormals[i].z;
		}

		if (ai_mesh->HasTextureCoords(0))
		{
			vertex.TexCoord.x = (float)ai_mesh->mTextureCoords[0][i].x;
			vertex.TexCoord.y = (float)ai_mesh->mTextureCoords[0][i].y;
		}
		else
		{
			vertex.TexCoord.x = 0.0;
			vertex.TexCoord.y = 0.0;
		}

		if (ai_mesh->HasTangentsAndBitangents())
		{
			vertex.Tangent.x = (float)ai_mesh->mTangents[i].x;
			vertex.Tangent.y = (float)ai_mesh->mTangents[i].y;
			vertex.Tangent.z = (float)ai_mesh->mTangents[i].z;

			vertex.Bitangent.x = (float)ai_mesh->mBitangents[i].x;
			vertex.Bitangent.y = (float)ai_mesh->mBitangents[i].y;
			vertex.Bitangent.z = (float)ai_mesh->mBitangents[i].z;
		}
		else
		{
			vertex.Tangent.x = 1.0;
			vertex.Tangent.y = 0.0;
			vertex.Tangent.z = 0.0;

			vertex.Bitangent.x = 1.0;
			vertex.Bitangent.y = 0.0;
			vertex.Bitangent.z = 0.0;
		}

		if (ai_mesh->HasVertexColors(i))
		{
			vertex.Color.x = (float)ai_mesh->mColors[0][i].r;
			vertex.Color.y = (float)ai_mesh->mColors[0][i].g;
			vertex.Color.z = (float)ai_mesh->mColors[0][i].b;
			vertex.Color.w = (float)ai_mesh->mColors[0][i].a;
		}
		else
		{
			vertex.Color.x = 1.0;
			vertex.Color.y = 1.0;
			vertex.Color.z = 1.0;
			vertex.Color.w = 1.0;
		}
		vertex.MaterialID = ai_mesh->mMaterialIndex;

		
		vertex.MeshID = CurrentID;
		vertices.push_back(vertex);

		materialID.push_back(vertex.MeshID);

	}
	CurrentID++;
	//index
	for (UINT i = 0; i < ai_mesh->mNumFaces; i++){
		aiFace ai_face = ai_mesh->mFaces[i];

		for (UINT j = 0; j < ai_face.mNumIndices; j++)
		{
			indices.push_back(ai_face.mIndices[j]);


		}
			
	}
	test.push_back(materialID);

	std::unique_ptr<Mesh> mesh = std::make_unique<Mesh>();
	const UINT vertexBufferSize = sizeof(Vertex_Model) * vertices.size();
	mesh->VertexBufferByteSize = vertexBufferSize;
	mesh->VertexByteStride = sizeof(Vertex_Model);
	mesh->VertexCount = vertices.size();
	mesh->VertexBufferGPU = helper::CreateDefaultBuffer(m_device, m_cmdList, vertices.data(), vertexBufferSize, mesh->VertexBufferUploader);

	const UINT indexBufferSize = static_cast<UINT>(indices.size()) * sizeof(UINT32);
	mesh->IndexBufferByteSize = indexBufferSize;
	mesh->IndexCount = indices.size();
	mesh->IndexBufferGPU = helper::CreateDefaultBuffer(m_device, m_cmdList, indices.data(), indexBufferSize, mesh->IndexBufferUploader);

	std::vector<UINT> indexInModelTextures{};

	if (ai_mesh->mMaterialIndex >= 0)
	{
		MeshMaterial meshMaterial;
		aiMaterial* ai_material = ai_scene->mMaterials[ai_mesh->mMaterialIndex];

		//if (m_textureType.empty())
		//	m_textureType = DetermineTextureType(ai_scene, ai_material);

		auto Loader = [&](aiTextureType type, std::string name, UINT& materialIndex) {
			std::vector<std::shared_ptr<Texture>> Maps;
			LoadMaterialTextures(ai_material, type, name, ai_scene, Maps, materialIndex);
			model.Textures.insert(model.Textures.end(), Maps.begin(), Maps.end());
			for (int i = 0; i < Maps.size(); i++) {
				indexInModelTextures.push_back(m_indexInTextureLoader++);
			}
		};

		Loader(aiTextureType_DIFFUSE, "texture_diffuse", meshMaterial.albedo_map);
		Loader(aiTextureType_SPECULAR, "texture_specular", meshMaterial.spec_map);
		Loader(aiTextureType_NORMALS, "texture_normal", meshMaterial.normal_map);
		Loader(aiTextureType_EMISSIVE, "texture_emissive", meshMaterial.emissive_map);

		//std::vector<std::shared_ptr<Texture>> diffuseMaps;
		//LoadMaterialTextures(ai_material,aiTextureType_DIFFUSE, "texture_diffuse", ai_scene, diffuseMaps);
		//model.Textures.insert(model.Textures.end(),diffuseMaps.begin(),diffuseMaps.end());
		//for (int i = 0; i < diffuseMaps.size(); i++) {
		//	indexInModelTextures.push_back(m_indexInTextureLoader++);
		//}

		//std::vector<std::shared_ptr<Texture>> specMaps;
		//LoadMaterialTextures(ai_material, aiTextureType_SPECULAR, "texture_specular", ai_scene, specMaps);
		//model.Textures.insert(model.Textures.end(), specMaps.begin(), specMaps.end());
		//for (int i = 0; i < specMaps.size(); i++) {
		//	indexInModelTextures.push_back(m_indexInTextureLoader++);
		//}

		//std::vector<std::shared_ptr<Texture>> normalMaps;
		//LoadMaterialTextures(ai_material, aiTextureType_NORMALS, "texture_normal", ai_scene, normalMaps);
		//model.Textures.insert(model.Textures.end(), normalMaps.begin(), normalMaps.end());
		//for (int i = 0; i < normalMaps.size(); i++) {
		//	indexInModelTextures.push_back(m_indexInTextureLoader++);
		//}

		//std::vector<std::shared_ptr<Texture>> emissiveMaps;
		//LoadMaterialTextures(ai_material, aiTextureType_EMISSIVE, "texture_emissive", ai_scene, emissiveMaps);
		//model.Textures.insert(model.Textures.end(), emissiveMaps.begin(), emissiveMaps.end());
		//for (int i = 0; i < emissiveMaps.size(); i++) {
		//	indexInModelTextures.push_back(m_indexInTextureLoader++);
		//}

		//aiTextureType type[13]{ aiTextureType_NONE,
		//	aiTextureType_DIFFUSE , //24
		//	aiTextureType_SPECULAR, //1
		//	aiTextureType_AMBIENT,
		//	aiTextureType_EMISSIVE,
		//	aiTextureType_HEIGHT, //10
		//	aiTextureType_NORMALS,
		//	aiTextureType_SHININESS,
		//	aiTextureType_OPACITY, //3
		//	aiTextureType_DISPLACEMENT,
		//	aiTextureType_LIGHTMAP,
		//	aiTextureType_REFLECTION,
		//	aiTextureType_UNKNOWN };

		//for (int i = 0; i < 13; ++i)
		//	Loader(type[i], "texture_" + std::to_string(i));


		model.MaterialID.push_back(ai_mesh->mMaterialIndex);
		model.Mat.push_back(meshMaterial);

	}
	//model.Meshes[std::move(mesh)] = indexInModelTextures;
	model.Meshes.push_back({ std::move(mesh),indexInModelTextures });

	return true;
}

bool ModelLoader::LoadMaterialTextures(aiMaterial* ai_mat, aiTextureType ai_texType, //materialIndex用于供bindless使用
	std::string typeName, const aiScene* ai_scene, std::vector<std::shared_ptr<Texture>>& textures, UINT& materialIndex)
{
	bool result = false;
	
	std::vector<std::shared_ptr<Texture>>& textureLoaded = m_textureLoader->GetTextureLoaded();
	for (UINT i = 0; i < ai_mat->GetTextureCount(ai_texType); i++)
	{
		aiString str;
		ai_mat->GetTexture(ai_texType, i, &str);
		// Check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
		bool skip = false;

		for (UINT j = 0; j < textureLoaded.size(); j++)
		{
			std::string tempstr = m_modelDic + "/" + std::string(str.C_Str());
			if (std::strcmp(textureLoaded[j]->FileName.c_str(), tempstr.c_str()) == 0)
			{
				textures.push_back(textureLoaded[j]);
				materialIndex = j;//所用纹理已被加载，索引等于目前检测为重复的纹理
				skip = true; // A texture with the same filepath has already been loaded, continue to next one. (optimization)
				break;
			}
		}

		if (!skip)
		{   // If texture hasn't been loaded already, load it
			std::shared_ptr<Texture> texture = std::make_shared<Texture>();
			texture->Type = typeName;

			std::string filename = std::string(str.C_Str());
			filename = m_modelDic + "/" + filename;

			result = m_textureLoader->Load(filename,texture);
			if (!result){
				return false;
			}
			materialIndex = textureLoaded.size() - 1; //所用纹理未被加载，则索引等于当前最新加载的纹理
			textures.push_back(texture);
			countMap[ai_texType]++;
		}
	}
	return true;
}

std::string ModelLoader::DetermineTextureType(const aiScene* ai_scene, aiMaterial* ai_mat)
{
	aiString textypeStr;
	ai_mat->GetTexture(aiTextureType_DIFFUSE, 0, &textypeStr);
	std::string textypeteststr = textypeStr.C_Str();
	if (textypeteststr == "*0" || textypeteststr == "*1" || textypeteststr == "*2" || textypeteststr == "*3" || textypeteststr == "*4" || textypeteststr == "*5")
	{
		if (ai_scene->mTextures[0]->mHeight == 0)
		{
			return "embedded compressed texture";
		}
		else
		{
			return "embedded non-compressed texture";
		}
	}
	if (textypeteststr.find('.') != std::string::npos)
	{
		return "textures are on disk";
	}

	return "null";
}

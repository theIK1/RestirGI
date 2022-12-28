#pragma once
#include "helper/CS/createCS.h"


class Indirect
{
public:

	void Execute(ID3D12GraphicsCommandList* cmdList,
		ID3D12RootSignature* rootSig,
		ID3D12PipelineState* PSO,
		ID3D12Resource* input,
		ID3D12DescriptorHeap* rtSrvUavHeap
	);



	



};
# ReSTIR

https://github.com/EmbarkStudios/kajiya
用DXR 和 c++ 复现相应工程，实现实时的ReSTIR GI效果(包含diffuse和glossy)

基本目标：
1.能实现正确的实时ReSTIR GI效果(包含diffuse和glossy)
2.能加载不同的展示场景（可以选择glTF或USD作为场景格式）
3.能较正确处理HDR曝光问题
额外目标：
能保持稳定的高帧率运行
实现基于path tracing的对比效果
能实现动态的场景加载以及streaming
使用async queue辅助渲染、加载

# Building and Running

打开RayTracing目录下的RayTracing.sln，需要安装visual stdio 2019.
生成解决方案，得到一个bin目录，然后将Libraries目录中的assimp-vc140-mt.dll，
dxcompiler.dll和dxil.dll复制到bin目录下，然后可以运行查看结果
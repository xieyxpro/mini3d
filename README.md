# mini3d
3D Software Render Engine

3D软件渲染，并没有任何性能优化，主要向人说明如何写一个固定管线的软件渲染器，有三种模式。

## 视频展示
[视频 By B站](https://www.bilibili.com/video/av14108626/)

## 特性
- 单个文件：源代码只有一个 mini3d.c，单个文件实现所有内容，容易阅读。
- 独立编译：没有任何第三方库依赖，没有复杂的工程目录。
- 模型标准：标准 D3D 坐标模型，左手系加 WORLD / VIEW / PROJECTION 三矩阵
- 实现裁剪：简单 CVV 裁剪
- 纹理支持：最大支持 1024 x 1024 的纹理
- 透视贴图：透视纹理映射以及透视色彩填充
- 边缘计算：精确的多边形边缘覆盖计算
- 实现精简：渲染引擎只有 700行，模块清晰，主干突出。
- 详细注释：主要代码详细注释

## 编译
- mingw: gcc -O3 mini3d.c -o mini3d.exe -lgdi32
- msvc: cl -O2 -nologo mini3d.c

## 演示
纹理填充：RENDER_STATE_TEXTURE 
![image](https://github.com/xieyxpro/mini3d/blob/master/image/%E6%8D%95%E8%8E%B7.PNG)

色彩填充：RENDER_STATE_COLOR 
![image](https://github.com/xieyxpro/mini3d/blob/master/image/%E6%8D%95%E8%8E%B71.PNG)

线框绘制：RENDER_STATE_WIREFRAME 
![image](https://github.com/xieyxpro/mini3d/blob/master/image/%E6%8D%95%E8%8E%B72.PNG)

##TODO
- 增加背面剔除
- 增加简单光照
- 提供更多渲染模式
- 实现二次线性差值的纹理读取
- 优化顶点计算性能
- 优化 draw_scanline 性能
- 从 BMP/TGA 文件加载纹理
- 载入 BSP 场景并实现漫游

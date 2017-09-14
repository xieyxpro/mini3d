//=====================================================================
// build:
//   mingw: gcc -O3 mini3d.c -o mini3d.exe -lgdi32
//   msvc:  cl -O2 -nologo mini3d.c 
//=====================================================================
#include <stdio.h>
#include <stdlib.h>
#include "3dMath.h"
#include <assert.h>

#include <windows.h>
#include <tchar.h>

typedef unsigned int IUINT32;

#define RENDER_STATE_WIREFRAME      1       // 渲染线框
#define RENDER_STATE_TEXTURE        2       // 渲染纹理
#define RENDER_STATE_COLOR          4       // 渲染颜色

int REMOVE_BACKFACE = 1;      				// 背面消除

// 根据三角形生成 0-2 个梯形，并且返回合法梯形的数量
int trapezoid_init_triangle(trapezoid_t *trap, const vertex_t *p1, 
    const vertex_t *p2, const vertex_t *p3) {
    const vertex_t *p;
    float k, x;

    if (p1->pos.y > p2->pos.y) p = p1, p1 = p2, p2 = p;
    if (p1->pos.y > p3->pos.y) p = p1, p1 = p3, p3 = p;
    if (p2->pos.y > p3->pos.y) p = p2, p2 = p3, p3 = p;
    if (p1->pos.y == p2->pos.y && p1->pos.y == p3->pos.y) return 0;
    if (p1->pos.x == p2->pos.x && p1->pos.x == p3->pos.x) return 0;

    if (p1->pos.y == p2->pos.y) {   // triangle down
        if (p1->pos.x > p2->pos.x) p = p1, p1 = p2, p2 = p;
        trap[0].top = p1->pos.y;
        trap[0].bottom = p3->pos.y;
        trap[0].left.v1 = *p1;
        trap[0].left.v2 = *p3;
        trap[0].right.v1 = *p2;
        trap[0].right.v2 = *p3;
        return (trap[0].top < trap[0].bottom)? 1 : 0;
    }

    if (p2->pos.y == p3->pos.y) {   // triangle up
        if (p2->pos.x > p3->pos.x) p = p2, p2 = p3, p3 = p;
        trap[0].top = p1->pos.y;
        trap[0].bottom = p3->pos.y;
        trap[0].left.v1 = *p1;
        trap[0].left.v2 = *p2;
        trap[0].right.v1 = *p1;
        trap[0].right.v2 = *p3;
        return (trap[0].top < trap[0].bottom)? 1 : 0;
    }

    trap[0].top = p1->pos.y;
    trap[0].bottom = p2->pos.y;
    trap[1].top = p2->pos.y;
    trap[1].bottom = p3->pos.y;

    k = (p3->pos.y - p1->pos.y) / (p2->pos.y - p1->pos.y);
    x = p1->pos.x + (p2->pos.x - p1->pos.x) * k;

    if (x <= p3->pos.x) {       // triangle left
        trap[0].left.v1 = *p1;
        trap[0].left.v2 = *p2;
        trap[0].right.v1 = *p1;
        trap[0].right.v2 = *p3;
        trap[1].left.v1 = *p2;
        trap[1].left.v2 = *p3;
        trap[1].right.v1 = *p1;
        trap[1].right.v2 = *p3;
    }   else {                  // triangle right
        trap[0].left.v1 = *p1;
        trap[0].left.v2 = *p3;
        trap[0].right.v1 = *p1;
        trap[0].right.v2 = *p2;
        trap[1].left.v1 = *p1;
        trap[1].left.v2 = *p3;
        trap[1].right.v1 = *p2;
        trap[1].right.v2 = *p3;
    }

    return 2;
}

// 按照 Y 坐标计算出左右两条边纵坐标等于 Y 的顶点
void trapezoid_edge_interp(trapezoid_t *trap, float y) {
    float s1 = trap->left.v2.pos.y - trap->left.v1.pos.y;
    float s2 = trap->right.v2.pos.y - trap->right.v1.pos.y;
    float t1 = (y - trap->left.v1.pos.y) / s1;
    float t2 = (y - trap->right.v1.pos.y) / s2;
    vertex_interp(&trap->left.v, &trap->left.v1, &trap->left.v2, t1);
    vertex_interp(&trap->right.v, &trap->right.v1, &trap->right.v2, t2);
}

// 根据左右两边的端点，初始化计算出扫描线的起点和步长
void trapezoid_init_scan_line(const trapezoid_t *trap, scanline_t *scanline, int y) {
    float width = trap->right.v.pos.x - trap->left.v.pos.x;
    scanline->x = (int)(trap->left.v.pos.x + 0.5f);
    scanline->w = (int)(trap->right.v.pos.x + 0.5f) - scanline->x;
    scanline->y = y;
    scanline->v = trap->left.v;
    if (trap->left.v.pos.x >= trap->right.v.pos.x) scanline->w = 0;
    vertex_division(&scanline->step, &trap->left.v, &trap->right.v, width);
}


//=====================================================================
// 渲染设备
//=====================================================================
typedef struct {
    transform_t transform;      // 坐标变换器
    int width;                  // 窗口宽度
    int height;                 // 窗口高度
    IUINT32 **framebuffer;      // 像素缓存：framebuffer[y] 代表第 y行
    float **zbuffer;            // 深度缓存：zbuffer[y] 为第 y行指针
    IUINT32 **texture;          // 纹理：同样是每行索引
    int tex_width;              // 纹理宽度
    int tex_height;             // 纹理高度
    float max_u;                // 纹理最大宽度：tex_width - 1
    float max_v;                // 纹理最大高度：tex_height - 1
    int render_state;           // 渲染状态
    IUINT32 background;         // 背景颜色
    IUINT32 foreground;         // 线框颜色
}   device_t;

IUINT32 reflectIndex;//反射指数
float ambientLightIntensity, lightIntensity, diffuseRate, specularRate;//环境光强度, 平行光源光强度, 漫反射系数, 镜面反射系数
vector_t lightDirection, upDirection, viewDirection;//平行光源方向, 视线法向量，视线方向，变换后的视线方向
point_t cameraPosition, viewPosition;//摄影机位置，视点位置

// 设备初始化，fb为外部帧缓存，非 NULL 将引用外部帧缓存（每行 4字节对齐）
void device_init(device_t *device, int width, int height, void *fb) {
    int need = sizeof(void*) * (height * 2 + 1024) + width * height * 8;
    char *ptr = (char*)malloc(need + 64);
    char *framebuf, *zbuf;
    int j;
    assert(ptr);
    device->framebuffer = (IUINT32**)ptr;
    device->zbuffer = (float**)(ptr + sizeof(void*) * height);
    ptr += sizeof(void*) * height * 2;
    device->texture = (IUINT32**)ptr;
    ptr += sizeof(void*) * 1024;
    framebuf = (char*)ptr;
    zbuf = (char*)ptr + width * height * 4;
    ptr += width * height * 8;
    if (fb != NULL) framebuf = (char*)fb;
    for (j = 0; j < height; j++) {
        device->framebuffer[j] = (IUINT32*)(framebuf + width * 4 * j);
        device->zbuffer[j] = (float*)(zbuf + width * 4 * j);
    }
    device->texture[0] = (IUINT32*)ptr;
    device->texture[1] = (IUINT32*)(ptr + 16);
    memset(device->texture[0], 0, 64);
    device->tex_width = 2;
    device->tex_height = 2;
    device->max_u = 1.0f;
    device->max_v = 1.0f;
    device->width = width;
    device->height = height;
    device->background = 0xffc300;
    device->foreground = 0;
    transform_init(&device->transform, width, height);
    device->render_state = RENDER_STATE_WIREFRAME;
}

// 删除设备
void device_destroy(device_t *device) {
    if (device->framebuffer) 
        free(device->framebuffer);
    device->framebuffer = NULL;
    device->zbuffer = NULL;
    device->texture = NULL;
}

// 设置当前纹理
void device_set_texture(device_t *device, void *bits, long pitch, int w, int h) {
    IUINT32 *ptr = (IUINT32*)bits;
    int j;
    assert(w <= 1024 && h <= 1024);
    for (j = 0; j < h; ptr += pitch, j++)   // 重新计算每行纹理的指针
        device->texture[j] = (IUINT32*)ptr;
    device->tex_width = w;
    device->tex_height = h;
    device->max_u = (float)(w - 1);
    device->max_v = (float)(h - 1);
}

// 清空 framebuffer 和 zbuffer
void device_clear(device_t *device, int mode) {
    int y, x, height = device->height;
    for (y = 0; y < device->height; y++) {
        IUINT32 *dst = device->framebuffer[y];
        IUINT32 cc = (height - 1 - y) * 230 / (height - 1);
        cc = (cc << 16) | (cc << 8) | cc;
        if (mode == 0) cc = device->background;
        for (x = device->width; x > 0; dst++, x--) dst[0] = cc;
    }
    for (y = 0; y < device->height; y++) {
        float *dst = device->zbuffer[y];
        for (x = device->width; x > 0; dst++, x--) dst[0] = 0.0f;
    }
}

// 画点
void device_pixel(device_t *device, int x, int y, IUINT32 color) {
    if (((IUINT32)x) < (IUINT32)device->width && ((IUINT32)y) < (IUINT32)device->height) {
        device->framebuffer[y][x] = color;
    }
}

// 绘制线段
void device_draw_line(device_t *device, int x1, int y1, int x2, int y2, IUINT32 c) {
    int x, y, rem = 0;
    if (x1 == x2 && y1 == y2) {
        device_pixel(device, x1, y1, c);
    }   else if (x1 == x2) {
        int inc = (y1 <= y2)? 1 : -1;
        for (y = y1; y != y2; y += inc) device_pixel(device, x1, y, c);
        device_pixel(device, x2, y2, c);
    }   else if (y1 == y2) {
        int inc = (x1 <= x2)? 1 : -1;
        for (x = x1; x != x2; x += inc) device_pixel(device, x, y1, c);
        device_pixel(device, x2, y2, c);
    }   else {
        int dx = (x1 < x2)? x2 - x1 : x1 - x2;
        int dy = (y1 < y2)? y2 - y1 : y1 - y2;
        if (dx >= dy) {
            if (x2 < x1) x = x1, y = y1, x1 = x2, y1 = y2, x2 = x, y2 = y;
            for (x = x1, y = y1; x <= x2; x++) {
                device_pixel(device, x, y, c);
                rem += dy;
                if (rem >= dx) {
                    rem -= dx;
                    y += (y2 >= y1)? 1 : -1;
                    device_pixel(device, x, y, c);
                }
            }
            device_pixel(device, x2, y2, c);
        }   else {
            if (y2 < y1) x = x1, y = y1, x1 = x2, y1 = y2, x2 = x, y2 = y;
            for (x = x1, y = y1; y <= y2; y++) {
                device_pixel(device, x, y, c);
                rem += dx;
                if (rem >= dy) {
                    rem -= dy;
                    x += (x2 >= x1)? 1 : -1;
                    device_pixel(device, x, y, c);
                }
            }
            device_pixel(device, x2, y2, c);
        }
    }
}

// 根据坐标读取纹理
IUINT32 device_texture_read(const device_t *device, float u, float v) {
    int x, y;
    u = u * device->max_u;
    v = v * device->max_v;
    x = (int)(u + 0.5f);
    y = (int)(v + 0.5f);
    x = CMID(x, 0, device->tex_width - 1);
    y = CMID(y, 0, device->tex_height - 1);
    return device->texture[y][x];
}


//=====================================================================
// 渲染实现
//=====================================================================

// 绘制扫描线
void device_draw_scanline(device_t *device, scanline_t *scanline) {
    IUINT32 *framebuffer = device->framebuffer[scanline->y];
    float *zbuffer = device->zbuffer[scanline->y];
    int x = scanline->x;
    int w = scanline->w;
    int width = device->width;
    int render_state = device->render_state;
    for (; w > 0; x++, w--) {
        if (x >= 0 && x < width) {
            float rhw = scanline->v.rhw;
            if (rhw >= zbuffer[x]) {    
                float w = 1.0f / rhw;
                zbuffer[x] = rhw;
                if (render_state & RENDER_STATE_COLOR) {
                    float r = scanline->v.color.r * w;
                    float g = scanline->v.color.g * w;
                    float b = scanline->v.color.b * w;
                    int R = (int)(r * 255.0f);
                    int G = (int)(g * 255.0f);
                    int B = (int)(b * 255.0f);
                    R = CMID(R, 0, 255);
                    G = CMID(G, 0, 255);
                    B = CMID(B, 0, 255);
                    framebuffer[x] = (R << 16) | (G << 8) | (B);
                }
                if (render_state & RENDER_STATE_TEXTURE) {
                    float u = scanline->v.tc.u * w;
                    float v = scanline->v.tc.v * w;
                    IUINT32 cc = device_texture_read(device, u, v);
                    framebuffer[x] = ((int)((cc >> 16) * scanline->v.light) << 16) +
                                     ((int)(((cc & 65535)>> 8) * scanline->v.light) << 8) +
                                     (int)((cc & 255) * scanline->v.light);
                }
            }
        }
        vertex_add(&scanline->v, &scanline->step);
        if (x >= width) break;
    }
}

// 主渲染函数
void device_render_trap(device_t *device, trapezoid_t *trap) {
    scanline_t scanline;
    int j, top, bottom;
    top = (int)(trap->top + 0.5f);
    bottom = (int)(trap->bottom + 0.5f);
    for (j = top; j < bottom; j++) {
        if (j >= 0 && j < device->height) {
            trapezoid_edge_interp(trap, (float)j + 0.5f);
            trapezoid_init_scan_line(trap, &scanline, j);
            device_draw_scanline(device, &scanline);
        }
        if (j >= device->height) break;
    }
}

// 根据 render_state 绘制原始三角形
void device_draw_primitive(device_t *device, const vertex_t *v1, 
    const vertex_t *v2, const vertex_t *v3, const vector_t *normal) {
	if (REMOVE_BACKFACE) {
		//在世界坐标系下进行背面消除
		vector_t u, v, normal_backTest, view_backTest;
		point_sub(&u, &v2->pos, &v1->pos);
		point_sub(&v, &v3->pos, &v1->pos);
		vector_sub(&view_backTest, &cameraPosition, &v1->pos);
		vector_crossproduct(&normal_backTest, &u, &v);
		if (vector_dotproduct(&normal_backTest, &view_backTest) < 0) return;//背面
	}	

    point_t p1, p2, p3, c1, c2, c3;
    vector_t trans_normal, trans_revViewDirection, 
             trans_revLightDirection, trans_reflectDirection;
    int render_state = device->render_state;
    matrix_t normal_transform;
    float ln;
    vector_t nnl;
    float t1_light, t2_light, t3_light;

    //变换图元法向量(旋转变换+摄影机变换)
    matrix_mul(&normal_transform, &device->transform.world, &device->transform.view);
    matrix_apply(&trans_normal, normal, &normal_transform);

    vector_normalize(&trans_normal);

    //变换光线方向(摄影机变换)
    matrix_apply(&trans_revLightDirection, &lightDirection, &device->transform.view);
    trans_revLightDirection.x = -trans_revLightDirection.x;
    trans_revLightDirection.y = -trans_revLightDirection.y;
    trans_revLightDirection.z = -trans_revLightDirection.z;

    vector_normalize(&trans_revLightDirection);

    ln = vector_dotproduct(&trans_revLightDirection, &trans_normal);
    nnl.x = 2 * ln * trans_normal.x;
    nnl.y = 2 * ln * trans_normal.y;
    nnl.z = 2 * ln * trans_normal.z;
    nnl.w = 0;
    vector_sub(&trans_reflectDirection, &nnl, &trans_revLightDirection);

    vector_normalize(&trans_reflectDirection);

    //计算能进入人眼的反射光方向
    c1.x = NEAR_PLANE * v1->pos.x / v1->pos.z;
    c1.y = NEAR_PLANE * v1->pos.y / v1->pos.z;
    c1.z = NEAR_PLANE;
    c1.w = 1;
    matrix_apply(&p1, &v1->pos, &device->transform.view);
    point_sub(&trans_revViewDirection, &c1, &p1);
    //point_sub(&trans_revViewDirection, &c1, &v1->pos);
    vector_normalize(&trans_revViewDirection);
    //printf("%lf %lf %lf %lf\n", trans_revViewDirection.x, trans_revViewDirection.y, trans_revViewDirection.z, trans_revViewDirection.w);
    t1_light = ambientLightIntensity + lightIntensity * (diffuseRate * ln + 
                                                     specularRate * 
                                 pow(vector_dotproduct(&trans_reflectDirection, &trans_revViewDirection), (float)reflectIndex));
    
    if (t1_light < ambientLightIntensity) t1_light = ambientLightIntensity;
    else if (t1_light > 1) t1_light = 1;
    
    c2.x = NEAR_PLANE * v2->pos.x / v2->pos.z;
    c2.y = NEAR_PLANE * v2->pos.y / v2->pos.z;
    c2.z = NEAR_PLANE;
    c2.w = 1;
    matrix_apply(&p2, &v2->pos, &device->transform.view);
    point_sub(&trans_revViewDirection, &c2, &p2);
    //point_sub(&trans_revViewDirection, &c2, &v2->pos);
    
    vector_normalize(&trans_revViewDirection);
    
    t2_light = ambientLightIntensity + lightIntensity * (diffuseRate * ln + 
                                                     specularRate * 
                                     pow(vector_dotproduct(&trans_reflectDirection, &trans_revViewDirection), (float)reflectIndex));

    if (t2_light < ambientLightIntensity) t2_light = ambientLightIntensity;
    else if (t2_light > 1) t2_light = 1;

    c3.x = NEAR_PLANE * v3->pos.x / v3->pos.z;
    c3.y = NEAR_PLANE * v3->pos.y / v3->pos.z;
    c3.z = NEAR_PLANE;
    c3.w = 1;
    matrix_apply(&p3, &v3->pos, &device->transform.view);
    point_sub(&trans_revViewDirection, &c3, &p3);
    //point_sub(&trans_revViewDirection, &c3, &v3->pos);
    
    vector_normalize(&trans_revViewDirection);
    
    t3_light = ambientLightIntensity + lightIntensity * (diffuseRate * ln + 
                                                     specularRate * 
                                 pow(vector_dotproduct(&trans_reflectDirection, &trans_revViewDirection), (float)reflectIndex));

    if (t3_light < ambientLightIntensity) t3_light = ambientLightIntensity;
    else if (t3_light > 1) t3_light = 1;

    // 按照 Transform 变化
    transform_apply(&device->transform, &c1, &v1->pos);
    transform_apply(&device->transform, &c2, &v2->pos);
    transform_apply(&device->transform, &c3, &v3->pos);

    // 裁剪，注意此处可以完善为具体判断几个点在 cvv内以及同cvv相交平面的坐标比例
    // 进行进一步精细裁剪，将一个分解为几个完全处在 cvv内的三角形
    if (transform_check_cvv(&c1) != 0) return;
    if (transform_check_cvv(&c2) != 0) return;
    if (transform_check_cvv(&c3) != 0) return;

    // 归一化
    transform_homogenize(&device->transform, &p1, &c1);
    transform_homogenize(&device->transform, &p2, &c2);
    transform_homogenize(&device->transform, &p3, &c3);

    // 纹理或者色彩绘制
    if (render_state & (RENDER_STATE_TEXTURE | RENDER_STATE_COLOR)) {
        vertex_t t1 = *v1, t2 = *v2, t3 = *v3;
        trapezoid_t traps[2];
        int n;

        t1.pos = p1; 
        t2.pos = p2;
        t3.pos = p3;
        t1.pos.w = c1.w;
        t2.pos.w = c2.w;
        t3.pos.w = c3.w;
        t1.light = t1_light;
        t2.light = t2_light;
        t3.light = t3_light;
        
        vertex_rhw_init(&t1);   // 初始化 w
        vertex_rhw_init(&t2);   // 初始化 w
        vertex_rhw_init(&t3);   // 初始化 w
        
        // 拆分三角形为0-2个梯形，并且返回可用梯形数量
        n = trapezoid_init_triangle(traps, &t1, &t2, &t3);

        if (n >= 1) device_render_trap(device, &traps[0]);
        if (n >= 2) device_render_trap(device, &traps[1]);
    }

    if (render_state & RENDER_STATE_WIREFRAME) {        // 线框绘制
        device_draw_line(device, (int)p1.x, (int)p1.y, (int)p2.x, (int)p2.y, device->foreground);
        device_draw_line(device, (int)p1.x, (int)p1.y, (int)p3.x, (int)p3.y, device->foreground);
        device_draw_line(device, (int)p3.x, (int)p3.y, (int)p2.x, (int)p2.y, device->foreground);
    }
}


//=====================================================================
// Win32 窗口及图形绘制：为 device 提供一个 DibSection 的 FB
//=====================================================================
int screen_w, screen_h, screen_exit = 0;
int screen_mx = 0, screen_my = 0, screen_mb = 0;
int screen_keys[512];   // 当前键盘按下状态
static HWND screen_handle = NULL;       // 主窗口 HWND
static HDC screen_dc = NULL;            // 配套的 HDC
static HBITMAP screen_hb = NULL;        // DIB
static HBITMAP screen_ob = NULL;        // 老的 BITMAP
unsigned char *screen_fb = NULL;        // frame buffer
long screen_pitch = 0;

int screen_init(int w, int h, const TCHAR *title);  // 屏幕初始化
int screen_close(void);                             // 关闭屏幕
void screen_dispatch(void);                         // 处理消息
void screen_update(void);                           // 显示 FrameBuffer

// win32 event handler
static LRESULT screen_events(HWND, UINT, WPARAM, LPARAM);   

#ifdef _MSC_VER
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#endif

// 初始化窗口并设置标题
int screen_init(int w, int h, const TCHAR *title) {
    WNDCLASS wc = { CS_BYTEALIGNCLIENT, (WNDPROC)screen_events, 0, 0, 0, 
        NULL, NULL, NULL, NULL, _T("SCREEN3.1415926") };
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 
        w * h * 4, 0, 0, 0, 0 }  };
    RECT rect = { 0, 0, w, h };
    int wx, wy, sx, sy;
    LPVOID ptr;
    HDC hDC;

    screen_close();

    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClass(&wc)) return -1;

    screen_handle = CreateWindow(_T("SCREEN3.1415926"), title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (screen_handle == NULL) return -2;

    screen_exit = 0;
    hDC = GetDC(screen_handle);
    screen_dc = CreateCompatibleDC(hDC);
    ReleaseDC(screen_handle, hDC);

    screen_hb = CreateDIBSection(screen_dc, &bi, DIB_RGB_COLORS, &ptr, 0, 0);
    if (screen_hb == NULL) return -3;

    screen_ob = (HBITMAP)SelectObject(screen_dc, screen_hb);
    screen_fb = (unsigned char*)ptr;
    screen_w = w;
    screen_h = h;
    screen_pitch = w * 4;
    
    AdjustWindowRect(&rect, GetWindowLong(screen_handle, GWL_STYLE), 0);
    wx = rect.right - rect.left;
    wy = rect.bottom - rect.top;
    sx = (GetSystemMetrics(SM_CXSCREEN) - wx) / 2;
    sy = (GetSystemMetrics(SM_CYSCREEN) - wy) / 2;
    if (sy < 0) sy = 0;
    SetWindowPos(screen_handle, NULL, sx, sy, wx, wy, (SWP_NOCOPYBITS | SWP_NOZORDER | SWP_SHOWWINDOW));
    SetForegroundWindow(screen_handle);

    ShowWindow(screen_handle, SW_NORMAL);
    screen_dispatch();

    memset(screen_keys, 0, sizeof(int) * 512);
    memset(screen_fb, 0, w * h * 4);

    return 0;
}

int screen_close(void) {
    if (screen_dc) {
        if (screen_ob) { 
            SelectObject(screen_dc, screen_ob); 
            screen_ob = NULL; 
        }
        DeleteDC(screen_dc);
        screen_dc = NULL;
    }
    if (screen_hb) { 
        DeleteObject(screen_hb); 
        screen_hb = NULL; 
    }
    if (screen_handle) { 
        CloseWindow(screen_handle); 
        screen_handle = NULL; 
    }
    return 0;
}

static LRESULT screen_events(HWND hWnd, UINT msg, 
    WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE: screen_exit = 1; break;
    case WM_KEYDOWN: screen_keys[wParam & 511] = 1; break;
    case WM_KEYUP: screen_keys[wParam & 511] = 0; break;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void screen_dispatch(void) {
    MSG msg;
    while (1) {
        if (!PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) break;
        if (!GetMessage(&msg, NULL, 0, 0)) break;
        DispatchMessage(&msg);
    }
}

void screen_update(void) {
    HDC hDC = GetDC(screen_handle);
    BitBlt(hDC, 0, 0, screen_w, screen_h, screen_dc, 0, 0, SRCCOPY);
    ReleaseDC(screen_handle, hDC);
    screen_dispatch();
}


//=====================================================================
// 主程序
//=====================================================================
vertex_t mesh[8] = {
    { {  1, -1,  1, 1 }, { 0, 0 }, { 1.0f, 0.2f, 0.2f }, 1 },
    { { -1, -1,  1, 1 }, { 0, 1 }, { 0.2f, 1.0f, 0.2f }, 1 },
    { { -1,  1,  1, 1 }, { 1, 1 }, { 0.2f, 0.2f, 1.0f }, 1 },
    { {  1,  1,  1, 1 }, { 1, 0 }, { 1.0f, 0.2f, 1.0f }, 1 },
    { {  1, -1, -1, 1 }, { 0, 0 }, { 1.0f, 1.0f, 0.2f }, 1 },
    { { -1, -1, -1, 1 }, { 0, 1 }, { 0.2f, 1.0f, 1.0f }, 1 },
    { { -1,  1, -1, 1 }, { 1, 1 }, { 1.0f, 0.3f, 0.3f }, 1 },
    { {  1,  1, -1, 1 }, { 1, 0 }, { 0.2f, 1.0f, 0.3f }, 1 },
};

vector_t nor[6] = {{0, 0, 1, 0}, {0, 0, -1, 0}, {0, -1, 0, 0}, 
                   {-1, 0, 0, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}};

void draw_plane(device_t *device, int a, int b, int c, int d, vector_t normal) {
    vertex_t p1 = mesh[a], p2 = mesh[b], p3 = mesh[c], p4 = mesh[d];
    p1.tc.u = 0, p1.tc.v = 0, p2.tc.u = 0, p2.tc.v = 1;
    p3.tc.u = 1, p3.tc.v = 1, p4.tc.u = 1, p4.tc.v = 0;
    device_draw_primitive(device, &p1, &p2, &p3, &normal);
    device_draw_primitive(device, &p3, &p4, &p1, &normal);
}

void draw_box(device_t *device, float theta) {
    matrix_t m;

    matrix_set_rotate(&m, -1, 1, 1, theta);
    device->transform.world = m;
    transform_update(&device->transform);
    
    draw_plane(device, 0, 1, 2, 3, nor[0]);
    draw_plane(device, 6, 5, 4, 7, nor[1]);
    draw_plane(device, 5, 1, 0, 4, nor[2]);
    draw_plane(device, 1, 5, 6, 2, nor[3]);
    draw_plane(device, 2, 6, 7, 3, nor[4]);
    draw_plane(device, 3, 7, 4, 0, nor[5]);
}

void camera_at_zero(device_t *device, float x, float y, float z) {
    point_t eye = {x, y, z, 1}, at = {0, 0, 0, 1}, up = {0, 1, 0, 0};
    cameraPosition = eye;
    viewPosition = at;
    upDirection = up;
    point_sub(&viewDirection, &viewPosition, &cameraPosition);
    matrix_set_lookat(&device->transform.view, &cameraPosition, &viewPosition, &upDirection);
    transform_update(&device->transform);
}

void init_texture(device_t *device) {
    static IUINT32 texture[256][256];
    int i, j;
    for (j = 0; j < 256; j++) {
        for (i = 0; i < 256; i++) {
            int x = i / 32, y = j / 32;
            texture[j][i] = ((x + y) & 1)? 0xffffff : 0x3fbcef;
        }
    }
    device_set_texture(device, texture, 256, 256, 256);
}

void setAmbientLightIntensity(float intensity) {
    ambientLightIntensity = intensity;
}

void setLightDirection(float x, float y, float z) {
    lightDirection.x = x;
    lightDirection.y = y;
    lightDirection.z = z;
    lightDirection.w = 0;
}

void setLightIntensity(float value) {
    lightIntensity = value;
}

void setReflectIndex(IUINT32 n) {
    reflectIndex = n;
}

void setDiffuseRate(float r) {
    diffuseRate = r;
}

void setSpecularRate(float r) {
    specularRate = r;
}

int main(void)
{
    device_t device;
    int states[] = { RENDER_STATE_TEXTURE, RENDER_STATE_COLOR, RENDER_STATE_WIREFRAME };
    int indicator = 0;
    int kbhit = 0;//用于保证当空格键被持续按下时，显示模式只切换一次
    float alpha = 0;
    float pos = 5.5;

    TCHAR *title = _T("Mini3d (software render tutorial) - ")
        _T("Left/Right: rotation, Up/Down: forward/backward, Space: switch state");

    if (screen_init(800, 600, title)) 
        return -1;

    device_init(&device, 800, 600, screen_fb);

    setAmbientLightIntensity(0.25f);
    setLightDirection(-1, 0, -1);
    setLightIntensity(1.0);
    setReflectIndex(300);
    setDiffuseRate(0.6f);
    setSpecularRate(0.15f);
    init_texture(&device);
    device.render_state = RENDER_STATE_TEXTURE;

    while (screen_exit == 0 && screen_keys[VK_ESCAPE] == 0) {
        screen_dispatch();
        device_clear(&device, 0);
        camera_at_zero(&device, pos, 0, 0);
        
        if (screen_keys[VK_UP]) pos -= 0.01f;
        if (screen_keys[VK_DOWN]) pos += 0.01f;
        if (screen_keys[VK_LEFT]) alpha += 0.01f;
        if (screen_keys[VK_RIGHT]) alpha -= 0.01f;

        if (screen_keys[VK_TAB]) REMOVE_BACKFACE = (REMOVE_BACKFACE + 1) % 2;

        if (screen_keys[VK_SPACE]) {
            if (kbhit == 0) {
                kbhit = 1;
                if (++indicator >= 3) indicator = 0;
                device.render_state = states[indicator];
            }
        }   else {
            kbhit = 0;
        }

        draw_box(&device, alpha);
        screen_update();
        Sleep(1);
    }
    return 0;
}

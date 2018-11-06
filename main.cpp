#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define D3D_DEBUG_INFO
#include <Windows.h>
#include <shellapi.h>

#include <stdio.h>
#include <assert.h>

struct v2 { v2() {}; v2(float x_in, float y_in) { x = x_in; y = y_in; } float x, y; };
struct vertex_t { v2 pos; v2 uv; };
v2 operator*(v2 a, float b) { return v2(a.x * b, a.y * b); }
v2 operator*(v2 a, v2 b) { return v2(a.x * b.x, a.y * b.y); }
v2 operator+(v2 a, v2 b) { return v2(a.x + b.x, a.y + b.y); }
v2 operator-(v2 a, v2 b) { return v2(a.x - b.x, a.y - b.y); }

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#include <d3dx9.h>

#define USE_D3D_ERR_LIB

#ifdef USE_D3D_ERR_LIB
#	pragma comment(lib, "dxerr.lib")
#	pragma comment(lib, "legacy_stdio_definitions.lib")
#	include <DxErr.h>

	thread_local char d3d9_error_string_buffer[1024];
	const char* get_error_string_d3d9(HRESULT hr)
	{
		snprintf(d3d9_error_string_buffer, 1024, "%s, %s.", DXGetErrorString(hr), DXGetErrorDescription(hr));
		return d3d9_error_string_buffer;
	}

#	define ERROR_MSG(X) do { MessageBox(NULL, X, "Error", MB_ICONEXCLAMATION | MB_OK); } while (0)
#	define HR_CHECK(X) do { HRESULT hr = (X); if (FAILED(hr)) { ERROR_MSG(get_error_string_d3d9(hr)); __debugbreak(); } } while (0)
#endif

IDirect3DDevice9* dev;
IDirect3D9* d3d9;
IDirect3DTexture9* texture;
IDirect3DSurface9* surface;
IDirect3DSurface9* screen_surface;
int running = 1;

LRESULT CALLBACK wnd_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT rc;
	int dw, dh;

	GetClientRect(hWnd, &rc);
	dw = rc.right - rc.left;
	dh = rc.bottom - rc.top;

	switch (message)
	{
	case WM_CLOSE:
		running = 0;
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

int compile_shader(const char* vs, const char* ps, const char* vs_profile, const char* ps_profile, IDirect3DVertexShader9** vertex_shader, IDirect3DPixelShader9** pixel_shader, ID3DXConstantTable** vertex_shader_constant_table, ID3DXConstantTable** pixel_shader_constant_table)
{
	ID3DXBuffer* compiled_shader;
	ID3DXBuffer* error_msgs;

	HRESULT res = D3DXCompileShader(vs, (UINT)strlen(vs), NULL, NULL, "main", vs_profile, 0, &compiled_shader, &error_msgs, vertex_shader_constant_table);

	if (FAILED(res)) {
		const char* error_str = (const char*)error_msgs->GetBufferPointer();
		ERROR_MSG(error_str);
		return -1;
	} else {
		res = dev->CreateVertexShader((const DWORD*)compiled_shader->GetBufferPointer(), vertex_shader);
		if (FAILED(res)) {
			ERROR_MSG("Failed to create shader.");
			return -1;
		} else {
			compiled_shader->Release();
		}
	}

	res = D3DXCompileShader(ps, (UINT)strlen(ps), NULL, NULL, "main", ps_profile, 0, &compiled_shader, &error_msgs, pixel_shader_constant_table);

	if (FAILED(res)) {
		const char* error_str = (const char*)error_msgs->GetBufferPointer();
		ERROR_MSG(error_str);
		return -1;
	} else {
		res = dev->CreatePixelShader((const DWORD*)compiled_shader->GetBufferPointer(), pixel_shader);
		if (FAILED(res)) {
			ERROR_MSG("Failed to create shader.");
			return -1;
		} else {
			compiled_shader->Release();
		}
	}

	return 0;
}

int main(int argc, const char** argv)
{
	// Make a window.
	WNDCLASS wndclass = {0};
	const char* class_name = "Class Name D3D9 Render to Texture Example";
	wndclass.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wndclass.lpfnWndProc   = wnd_proc;
	wndclass.hInstance     = GetModuleHandle(NULL);
	wndclass.hIcon         = NULL;
	wndclass.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wndclass.lpszClassName = class_name;
	RegisterClass(&wndclass);

	HWND hwnd = CreateWindow(class_name, "D3D9 Render to Texture Example", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, wndclass.hInstance, NULL);
	if (!hwnd) return -1;

	// Setup D3D9.
	d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d9) {
		ERROR_MSG("Failed to initialize Direct3D 9 - the application was built against the correct header files.");
		return NULL;
	}

	D3DDISPLAYMODE mode;
	HRESULT res = d3d9->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);
	if (FAILED(res)) {
		ERROR_MSG("Direct3D 9 was unable to get adapter display mode.");
		return NULL;
	}

	res = d3d9->CheckDeviceType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format, D3DFMT_A8R8G8B8, true);
	if (FAILED(res)) {
		ERROR_MSG("HAL was detected as not supported by DirectD 9 for the D3DFMT_A8R8G8B8 adapter/backbuffer format.");
		return NULL;
	}

	D3DPRESENT_PARAMETERS params;
	D3DCAPS9 caps;

	res = d3d9->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
	if (FAILED(res)) {
		ERROR_MSG("Failed to gather Direct3D 9 device caps.");
		return NULL;
	}

	int flags = D3DCREATE_FPU_PRESERVE;
	if (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
		flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
		assert(caps.VertexProcessingCaps != 0);
	} else {
		flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
		assert(caps.VertexProcessingCaps);
	}

	if (caps.DevCaps & D3DDEVCAPS_PUREDEVICE) {
		flags |= D3DCREATE_PUREDEVICE;
	}

	memset(&params, 0, sizeof(params));
	params.BackBufferWidth = 640;
	params.BackBufferHeight = 480;
	params.BackBufferFormat = D3DFMT_A8R8G8B8;
	params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	params.hDeviceWindow = hwnd;
	params.Windowed = true;
	params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	res = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags, &params, &dev);
	if (FAILED(res)) {
		ERROR_MSG("Failed to create Direct3D 9 device.");
		return NULL;
	}

	dev = dev;
	const char* vs_profile = D3DXGetVertexShaderProfile(dev);
	const char* ps_profile = D3DXGetPixelShaderProfile(dev);
	vs_profile = "vs_2_0";
	ps_profile = "ps_2_0";

	if (vs_profile[3] < vs_profile[3]) {
		ERROR_MSG("The user machine does not support vertex shader profile 2.0.");
		return NULL;
	}

	if (ps_profile[3] < ps_profile[3]) {
		ERROR_MSG("The user machine does not support pixel shader profile 2.0.");
		return NULL;
	}

	res = dev->GetRenderTarget(0, &screen_surface);
	if (FAILED(res)) {
		ERROR_MSG("Unable to get render target.");
		return NULL;
	}

	HR_CHECK(dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE));
	HR_CHECK(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
	HR_CHECK(dev->SetRenderState(D3DRS_LIGHTING, FALSE));

	// Make checkerboard texture.
	IDirect3DTexture9* checker;
	res = dev->CreateTexture(8, 8, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &checker, NULL);
	if (FAILED(res)) {
		ERROR_MSG("Failed to create texture.");
		return -1;
	}

	unsigned pix[8 * 8];
	for (int i = 0; i < 8; ++i)
		for (int j = 0; j < 8; ++j)
			pix[i * 8 + j] = (i & 1) ^ (j & 1) ? 0xFF000000 : 0xFFFFFFFF;
	pix[7 * 8] = 0xFF777700;

	D3DLOCKED_RECT rect;
	HR_CHECK(checker->LockRect(0, &rect, NULL, D3DUSAGE_WRITEONLY));

	for (int i = 0; i < 8; ++i)
	{
		unsigned* row = (unsigned*)((char*)rect.pBits + (8 - i - 1) * rect.Pitch);
		for (int j = 0; j < 8; ++j)
		{
			// RBGA to BGRA
			unsigned pixel = ((unsigned*)pix)[i * 8 + j];
			pixel = ((pixel & 0x000000FF) << 16)
			      | ((pixel & 0x0000FF00))
			      | ((pixel & 0x00FF0000) >> 16)
			      | ((pixel & 0xFF000000));
			row[j] = pixel;
		}
	}

	HR_CHECK(checker->UnlockRect(0));

	HR_CHECK(D3DXSaveTextureToFile("checker.png", D3DXIFF_PNG, checker, NULL));

	// Create off-screen texture.
	res = dev->CreateTexture(640, 480, 0, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, NULL);
	if (FAILED(res)) {
		ERROR_MSG("Failed to create texture.");
		return -1;
	}

	// Grab surface from texture.
	HR_CHECK(texture->GetSurfaceLevel(0, &surface));

	// Create fullscreen shader for the full screen render texture.
	#define STR(X) #X
	const char* vs_fullscreen = STR(
		struct vertex_t
		{
			float2 pos : POSITION0;
			float2 uv  : TEXCOORD0;
		};

		struct interp_t
		{
			float4 posH : POSITION0;
			float2 uv   : TEXCOORD0;
		};

		interp_t main(vertex_t vtx)
		{
			float4 posH = float4(vtx.pos, 0, 1);

			interp_t interp;
			interp.posH = posH;
			interp.uv = vtx.uv;
			return interp;
		}
	);

	const char* ps_fullscreen = STR(
		struct interp_t
		{
			float4 posH : POSITION0;
			float2 uv   : TEXCOORD0;
		};

		sampler2D u_screen_image;

		float4 main(interp_t interp) : COLOR
		{
			float4 color = tex2D(u_screen_image, interp.uv);
			return color;
		}
	);

	IDirect3DVertexShader9* fullscreen_vertex_shader;
	IDirect3DPixelShader9* fullscreen_pixel_shader;
	ID3DXConstantTable* fullscreen_vertex_constant_table;
	ID3DXConstantTable* fullscreen_pixel_constant_table;
	int ret = compile_shader(vs_fullscreen, ps_fullscreen, vs_profile, ps_profile, &fullscreen_vertex_shader, &fullscreen_pixel_shader, &fullscreen_vertex_constant_table, &fullscreen_pixel_constant_table);
	if (ret) return ret;

	// Create render to texture shader.
	const char* vs_render_to_texture = STR(
		struct vertex_t
		{
			float2 pos : POSITION0;
			float2 uv  : TEXCOORD0;
		};

		struct interp_t
		{
			float4 posH : POSITION0;
			float2 uv   : TEXCOORD0;
		};

		float4x4 u_mvp;
		float u_x;

		interp_t main(vertex_t vtx)
		{
			vtx.pos.x += u_x;
			float4 posH = mul(u_mvp, float4(vtx.pos, 0, 1));

			interp_t interp;
			interp.posH = posH;
			interp.uv = vtx.uv;
			return interp;
		}
	);

	const char* ps_render_to_texture = STR(
		struct interp_t
		{
			float4 posH : POSITION0;
			float2 uv   : TEXCOORD0;
		};

		sampler2D u_image;

		float4 main(interp_t interp) : COLOR
		{
			float4 color = tex2D(u_image, interp.uv);
			return color;
		}
	);

	IDirect3DVertexShader9* render_to_texture_vertex_shader;
	IDirect3DPixelShader9* render_to_texture_pixel_shader;
	ID3DXConstantTable* render_to_texture_vertex_constant_table;
	ID3DXConstantTable* render_to_texture_pixel_constant_table;
	ret = compile_shader(vs_render_to_texture, ps_render_to_texture, vs_profile, ps_profile, &render_to_texture_vertex_shader, &render_to_texture_pixel_shader, &render_to_texture_vertex_constant_table, &render_to_texture_pixel_constant_table);
	if (ret) return ret;

	// Setup static vertex buffer for a full-screen quad.
	D3DVERTEXELEMENT9 desc[3];
	IDirect3DVertexDeclaration9* decl;
	for (int i = 0; i < 2; ++i)
	{
		desc[i].Stream = 0;
		desc[i].Offset = (WORD)(i * sizeof(v2));
		desc[i].Type = D3DDECLTYPE_FLOAT2;
		desc[i].Method = (BYTE)D3DDECLMETHOD_DEFAULT;
		desc[i].Usage = i == 0 ? (BYTE)D3DDECLUSAGE_POSITION : (BYTE)D3DDECLUSAGE_TEXCOORD;
		desc[i].UsageIndex = 0;
	}
	desc[2] = D3DDECL_END();
	HR_CHECK(dev->CreateVertexDeclaration(desc, &decl));

	IDirect3DVertexBuffer9* vertex_buffer;
	HR_CHECK(dev->CreateVertexBuffer(sizeof(vertex_t) * 6, 0, 0, D3DPOOL_MANAGED, &vertex_buffer, NULL));

	void* vertices;
	HR_CHECK(vertex_buffer->Lock(0, sizeof(vertex_t) * 6, &vertices, D3DLOCK_NOOVERWRITE));
	vertex_t quad[6];

	quad[0].pos = v2(-0.5f, 0.5f); quad[0].uv = v2(0, 0);
	quad[1].pos = v2(0.5f, -0.5f); quad[1].uv = v2(1, 1);
	quad[2].pos = v2(0.5f, 0.5f);  quad[2].uv = v2(1, 0);

	quad[3].pos = v2(-0.5f, 0.5f);  quad[3].uv = v2(0, 0);
	quad[4].pos = v2(-0.5f, -0.5f); quad[4].uv = v2(0, 1);
	quad[5].pos = v2(0.5f, -0.5f);  quad[5].uv = v2(1, 1);

	for (int i = 0; i < 6; ++i)
	{
		quad[i].pos = quad[i].pos * v2(2.0f, 2.0f);
	}

	memcpy(vertices, quad, sizeof(vertex_t) * 6);
	HR_CHECK(vertex_buffer->Unlock());

	// Setup global render state.
	HR_CHECK(dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE));
	HR_CHECK(dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
	HR_CHECK(dev->SetRenderState(D3DRS_LIGHTING, FALSE));
	HR_CHECK(dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_MIRROR));
	HR_CHECK(dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR));

	while (running)
	{
		UpdateWindow(hwnd);
		ShowWindow(hwnd, SW_SHOW);

		MSG msg;
		while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// Render checkerboard to texture.
		D3DXHANDLE uniform;
		HR_CHECK(dev->BeginScene());
		HR_CHECK(dev->SetRenderTarget(0, surface));
		HR_CHECK(dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFFF0000, 1.0f, 0));

			HR_CHECK(dev->SetVertexShader(render_to_texture_vertex_shader));
			HR_CHECK(dev->SetPixelShader(render_to_texture_pixel_shader));

			float identity[16];
			uniform = render_to_texture_vertex_constant_table->GetConstantByName(0, "u_mvp");
			memset(identity, 0, sizeof(identity));
			identity[0] = 1.0f;
			identity[5] = 1.0f;
			identity[10] = 1.0f;
			identity[15] = 1.0f;
			HR_CHECK(fullscreen_vertex_constant_table->SetValue(dev, uniform, identity, sizeof(float) * 16));

			static float x = 0;
			x += 0.001f;
			uniform = render_to_texture_vertex_constant_table->GetConstantByName(0, "u_x");
			HR_CHECK(fullscreen_vertex_constant_table->SetValue(dev, uniform, &x, sizeof(float)));

			HR_CHECK(dev->SetTexture(0, checker));

			HR_CHECK(dev->SetStreamSource(0, vertex_buffer, 0, sizeof(vertex_t)));
			HR_CHECK(dev->SetVertexDeclaration(decl));

			HR_CHECK(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 6));

		// Render texture to full-screen;
		HR_CHECK(dev->SetRenderTarget(0, screen_surface));
		HR_CHECK(dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF0000FF, 1.0f, 0));

			HR_CHECK(dev->SetVertexShader(fullscreen_vertex_shader));
			HR_CHECK(dev->SetPixelShader(fullscreen_pixel_shader));

			HR_CHECK(dev->SetTexture(0, texture));

			HR_CHECK(dev->SetStreamSource(0, vertex_buffer, 0, sizeof(vertex_t)));
			HR_CHECK(dev->SetVertexDeclaration(decl));

			HR_CHECK(dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 6));

		HR_CHECK(dev->EndScene());
		dev->Present(NULL, NULL, NULL, NULL);

		// Save rendered texture to file.
		HR_CHECK(D3DXSaveTextureToFile("render_to_texture.png", D3DXIFF_PNG, texture, NULL));
	}

	return 0;
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	int n, argc;
	LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
	const char **argv = (const char **)calloc(argc+1, sizeof(int));

	(void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

	for (n=0;n<argc;n++)
	{
		int len = WideCharToMultiByte(CP_UTF8, 0, wargv[n], -1, 0, 0, NULL, NULL);
		argv[n] = (char *)malloc(len);
		WideCharToMultiByte(CP_UTF8, 0, wargv[n], -1, (char*)argv[n], len, NULL, NULL);
	}

	return main(argc, argv);
}

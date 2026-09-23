// ChroniconMapReveal —— 右上角状态面板
//
// 用 CreateWindowEx(WS_EX_LAYERED) + UpdateLayeredWindow 画一个置顶的半透明面板,
// 显示两个机制的开关状态。为什么不用游戏的渲染层: 这个 Mod 只挂 GML 脚本函数,
// 拿不到 D3D11 的绘制阶段; 而 layered 窗口与游戏用什么 API 无关, 而且在窗口/无边框
// 模式下都能稳定显示 (真·独占全屏下系统不合成其他窗口, 届时面板不可见)。
//
// 文本渲染走 GDI: 先把文字画成白字黑底的单通道掩码, 再按颜色做一次 "over" 合成到
// 预乘 BGRA 的位图上 —— 这样既有抗锯齿, 又不用引入 GDI+ 依赖。

#include "overlay.hpp"

#include <windows.h>

#include <cstring>

namespace mr_overlay
{
	namespace
	{
		const wchar_t* const kWindowClass = L"ChroniconMapRevealStatusPanel";

		const UINT_PTR kTimerId = 1;
		const UINT kRefreshMs = 80;

		// 布局尺寸以 "参考分辨率" (游戏窗口宽 1512) 表达, 实际绘制时按窗口宽度等比缩放,
		// 这样不论分辨率/系统缩放如何, 面板在画面里的大小比例都一致
		const int kReferenceClientWidth = 1512;
		const int kMinScale = 75;
		const int kMaxScale = 175;

		const int kPanelWidth = 250;
		const int kPanelHeight = 98;
		const int kPadding = 13;
		const int kMarginRight = 14;
		const int kMarginTop = 74;	// 让开游戏右上角自带的区域名牌

		const wchar_t* const kTitle = L"Chronicon 地图全开";
		const wchar_t* const kLabelA = L"A 自动全开地图";
		const wchar_t* const kLabelB = L"B 绘制判定补丁";
		const wchar_t* const kHint = L"小键盘 1 / 2 切换 · 0 隐藏";
		const wchar_t* const kStateOn = L"开启";
		const wchar_t* const kStateOff = L"关闭";

		const COLORREF kColorTitle = RGB(170, 180, 198);
		const COLORREF kColorLabel = RGB(233, 237, 244);
		const COLORREF kColorHint = RGB(142, 152, 168);
		const COLORREF kColorOn = RGB(122, 229, 152);
		const COLORREF kColorOff = RGB(255, 138, 128);

		const int kBackgroundAlpha = 196;
		const int kHighlightAlpha = 224;	// 刚切换过状态时更实一点, 给个视觉反馈
		const ULONGLONG kHighlightMs = 1200;

		struct Surface
		{
			HDC dc = nullptr;
			HBITMAP bitmap = nullptr;
			HGDIOBJ previous = nullptr;
			unsigned char* pixels = nullptr;
			int width = 0;
			int height = 0;
		};

		Surface g_panel_surface;
		Surface g_mask_surface;

		HWND g_panel_window = nullptr;
		HINSTANCE g_instance = nullptr;
		HWND g_game_window = nullptr;

		volatile LONG g_started = 0;
		volatile LONG g_user_visible = 1;
		volatile LONG g_panel_shown = 0;

		const volatile long* g_state_a = nullptr;
		const volatile long* g_state_b = nullptr;
		void (*g_log)(const char*) = nullptr;

		HFONT g_font_title = nullptr;
		HFONT g_font_label = nullptr;
		HFONT g_font_state = nullptr;
		HFONT g_font_hint = nullptr;
		int g_scale = 100;	// 百分数

		bool g_have_rendered = false;
		bool g_surface_error_logged = false;
		LONG g_rendered_a = -1;
		LONG g_rendered_b = -1;
		int g_rendered_x = 0;
		int g_rendered_y = 0;
		int g_rendered_width = 0;
		int g_rendered_height = 0;
		int g_rendered_alpha = -1;

		LONG g_last_a = -1;
		LONG g_last_b = -1;
		ULONGLONG g_last_change_tick = 0;

		// ------------------------------------------------------------ helpers

		void LogMessage(const char* message)
		{
			if (g_log)
				g_log(message);
		}

		LONG ReadState(const volatile long* cell)
		{
			return InterlockedCompareExchange(const_cast<LONG*>(cell), 0, 0);
		}

		int Px(int logical)
		{
			return MulDiv(logical, g_scale, 100);
		}

		// 预乘 BGRA 的 "over" 合成: dst = src + dst * (1 - srcA)
		inline void BlendPixel(unsigned char* pixel, int blue, int green, int red, int alpha)
		{
			if (alpha <= 0)
				return;

			const int inverse = 255 - alpha;
			pixel[3] = static_cast<unsigned char>(alpha + pixel[3] * inverse / 255);
			pixel[0] = static_cast<unsigned char>(blue * alpha / 255 + pixel[0] * inverse / 255);
			pixel[1] = static_cast<unsigned char>(green * alpha / 255 + pixel[1] * inverse / 255);
			pixel[2] = static_cast<unsigned char>(red * alpha / 255 + pixel[2] * inverse / 255);
		}

		void FillRectAlpha(Surface& target, RECT rect, COLORREF color, int alpha)
		{
			if (!target.pixels)
				return;

			const int left = rect.left < 0 ? 0 : rect.left;
			const int top = rect.top < 0 ? 0 : rect.top;
			const int right = rect.right > target.width ? target.width : rect.right;
			const int bottom = rect.bottom > target.height ? target.height : rect.bottom;
			const int blue = GetBValue(color);
			const int green = GetGValue(color);
			const int red = GetRValue(color);

			for (int y = top; y < bottom; y++)
			{
				unsigned char* pixel = target.pixels + (static_cast<size_t>(y) * target.width + left) * 4;
				for (int x = left; x < right; x++, pixel += 4)
					BlendPixel(pixel, blue, green, red, alpha);
			}
		}

		// 把掩码里以亮度表示的覆盖度按指定颜色/透明度合成进 target
		void BlendMask(Surface& target, Surface& mask, COLORREF color, int alpha)
		{
			const size_t count = static_cast<size_t>(target.width) * target.height;
			const int blue = GetBValue(color);
			const int green = GetGValue(color);
			const int red = GetRValue(color);
			unsigned char* destination = target.pixels;
			const unsigned char* source = mask.pixels;

			for (size_t i = 0; i < count; i++, destination += 4, source += 4)
			{
				int coverage = source[0] > source[1] ? source[0] : source[1];
				if (source[2] > coverage)
					coverage = source[2];
				if (coverage == 0)
					continue;

				BlendPixel(destination, blue, green, red, coverage * alpha / 255);
			}
		}

		bool CreateSurface(Surface& surface, int width, int height)
		{
			if (surface.dc)
			{
				if (surface.previous)
					SelectObject(surface.dc, surface.previous);
				if (surface.bitmap)
					DeleteObject(surface.bitmap);
				DeleteDC(surface.dc);
			}
			surface = Surface{};

			if (width <= 0 || height <= 0)
				return false;

			surface.dc = CreateCompatibleDC(nullptr);
			if (!surface.dc)
				return false;

			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = width;
			info.bmiHeader.biHeight = -height;	// 自上而下, 与 UpdateLayeredWindow 一致
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;

			void* bits = nullptr;
			surface.bitmap = CreateDIBSection(surface.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
			if (!surface.bitmap || !bits)
			{
				if (surface.bitmap)
					DeleteObject(surface.bitmap);
				DeleteDC(surface.dc);
				surface = Surface{};
				return false;
			}

			surface.previous = SelectObject(surface.dc, surface.bitmap);
			surface.pixels = static_cast<unsigned char*>(bits);
			surface.width = width;
			surface.height = height;
			return true;
		}

		HFONT CreatePanelFont(int point_size, int weight)
		{
			LOGFONTW font{};
			font.lfHeight = -MulDiv(point_size, g_scale, 100);
			font.lfWeight = weight;
			font.lfCharSet = DEFAULT_CHARSET;
			font.lfOutPrecision = OUT_TT_PRECIS;
			// 掩码靠亮度取覆盖度, 所以必须用灰度抗锯齿, 不能用 ClearType
			font.lfQuality = ANTIALIASED_QUALITY;
			lstrcpynW(font.lfFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
			return CreateFontIndirectW(&font);
		}

		void RefreshFonts(int scale)
		{
			if (scale == g_scale && g_font_label)
				return;

			g_scale = scale;
			HFONT* fonts[] = { &g_font_title, &g_font_label, &g_font_state, &g_font_hint };
			for (HFONT* font : fonts)
			{
				if (*font)
				{
					DeleteObject(*font);
					*font = nullptr;
				}
			}

			g_font_title = CreatePanelFont(13, FW_NORMAL);
			g_font_label = CreatePanelFont(14, FW_NORMAL);
			g_font_state = CreatePanelFont(14, FW_BOLD);
			g_font_hint = CreatePanelFont(11, FW_NORMAL);
		}

		// 用白字黑底画一段文字拿到覆盖度掩码, 再按颜色合成到面板上
		void DrawTextElement(
			const wchar_t* text,
			RECT rect,
			HFONT font,
			UINT format,
			COLORREF color,
			int alpha
		)
		{
			if (!g_panel_surface.pixels || !g_mask_surface.pixels)
				return;
			if (g_mask_surface.width != g_panel_surface.width || g_mask_surface.height != g_panel_surface.height)
				return;
			if (!font)
				return;

			std::memset(g_mask_surface.pixels, 0, static_cast<size_t>(g_mask_surface.width) * g_mask_surface.height * 4);

			HGDIOBJ previous = SelectObject(g_mask_surface.dc, font);
			SetBkMode(g_mask_surface.dc, TRANSPARENT);
			SetTextColor(g_mask_surface.dc, RGB(255, 255, 255));
			DrawTextW(g_mask_surface.dc, text, -1, &rect, format | DT_NOPREFIX);
			SelectObject(g_mask_surface.dc, previous);

			BlendMask(g_panel_surface, g_mask_surface, color, alpha);
		}

		// ------------------------------------------------------------ drawing

		void DrawStateRow(int width, int top, const wchar_t* label, bool enabled)
		{
			const int padding = Px(kPadding);
			const int height = Px(22);

			DrawTextElement(
				label,
				RECT{ padding, top, width - Px(78), top + height },
				g_font_label,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE,
				kColorLabel,
				255
			);

			DrawTextElement(
				enabled ? kStateOn : kStateOff,
				RECT{ width - Px(78), top, width - padding, top + height },
				g_font_state,
				DT_RIGHT | DT_VCENTER | DT_SINGLELINE,
				enabled ? kColorOn : kColorOff,
				255
			);
		}

		void RenderPanel(int width, int height, bool state_a, bool state_b, int alpha)
		{
			std::memset(g_panel_surface.pixels, 0, static_cast<size_t>(width) * height * 4);

			FillRectAlpha(g_panel_surface, RECT{ 0, 0, width, height }, RGB(0, 0, 0), alpha);
			FillRectAlpha(g_panel_surface, RECT{ 0, 0, width, Px(2) }, RGB(255, 255, 255), 42);

			const int padding = Px(kPadding);
			DrawTextElement(
				kTitle,
				RECT{ padding, Px(8), width - padding, Px(28) },
				g_font_title,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE,
				kColorTitle,
				255
			);

			const int separator = Px(32);
			FillRectAlpha(g_panel_surface, RECT{ padding, separator, width - padding, separator + 1 }, RGB(255, 255, 255), 36);

			DrawStateRow(width, Px(36), kLabelA, state_a);
			DrawStateRow(width, Px(58), kLabelB, state_b);

			DrawTextElement(
				kHint,
				RECT{ padding, Px(78), width - padding, Px(96) },
				g_font_hint,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE,
				kColorHint,
				240
			);
		}

		// ------------------------------------------------------------ window

		struct FindWindowContext
		{
			DWORD process_id;
			HWND ignored;
			HWND best;
			long best_area;
		};

		BOOL CALLBACK FindWindowProc(HWND window, LPARAM parameter)
		{
			FindWindowContext* context = reinterpret_cast<FindWindowContext*>(parameter);
			if (window == context->ignored)
				return TRUE;

			DWORD process_id = 0;
			GetWindowThreadProcessId(window, &process_id);
			if (process_id != context->process_id)
				return TRUE;

			if (!IsWindowVisible(window) || IsIconic(window))
				return TRUE;
			if (GetWindow(window, GW_OWNER) != nullptr)
				return TRUE;

			const LONG style = GetWindowLongW(window, GWL_STYLE);
			const LONG ex_style = GetWindowLongW(window, GWL_EXSTYLE);
			if ((style & WS_CHILD) != 0 || (ex_style & WS_EX_TOOLWINDOW) != 0)
				return TRUE;

			RECT client{};
			if (!GetClientRect(window, &client))
				return TRUE;

			const long area = (client.right - client.left) * (client.bottom - client.top);
			if (area > context->best_area)
			{
				context->best_area = area;
				context->best = window;
			}
			return TRUE;
		}

		// 游戏窗口 = 本进程里面积最大的可见顶层窗口 (排除面板自己)
		HWND ResolveGameWindow()
		{
			if (g_game_window && IsWindow(g_game_window) && GetForegroundWindow() == g_game_window && !IsIconic(g_game_window))
				return g_game_window;

			FindWindowContext context{};
			context.process_id = GetCurrentProcessId();
			context.ignored = g_panel_window;
			context.best_area = 0;
			EnumWindows(&FindWindowProc, reinterpret_cast<LPARAM>(&context));

			g_game_window = context.best;
			return g_game_window;
		}

		void HidePanel()
		{
			if (g_panel_shown)
			{
				ShowWindow(g_panel_window, SW_HIDE);
				g_panel_shown = 0;
			}
			g_have_rendered = false;
			g_rendered_alpha = -1;
		}

		void UpdatePanel()
		{
			HWND game = ResolveGameWindow();
			RECT client{};
			bool usable = false;

			if (game && g_user_visible &&
				GetForegroundWindow() == game && !IsIconic(game) && IsWindowVisible(game) &&
				GetClientRect(game, &client))
			{
				const int client_width = client.right - client.left;
				const int client_height = client.bottom - client.top;
				POINT origin{ client.left, client.top };
				if (ClientToScreen(game, &origin))
				{
					client.left = origin.x;
					client.top = origin.y;
					client.right = origin.x + client_width;
					client.bottom = origin.y + client_height;
					usable = client_width > 64 && client_height > 64;
				}
			}

			if (!usable)
			{
				HidePanel();
				return;
			}

			int scale = MulDiv(client.right - client.left, 100, kReferenceClientWidth);
			if (scale < kMinScale)
				scale = kMinScale;
			if (scale > kMaxScale)
				scale = kMaxScale;
			RefreshFonts(scale);

			const int width = Px(kPanelWidth);
			const int height = Px(kPanelHeight);
			if (g_panel_surface.width != width || g_panel_surface.height != height)
			{
				if (!CreateSurface(g_panel_surface, width, height) || !CreateSurface(g_mask_surface, width, height))
				{
					if (!g_surface_error_logged)
					{
						LogMessage("overlay: could not create the status panel surface");
						g_surface_error_logged = true;
					}
					HidePanel();
					return;
				}
				g_surface_error_logged = false;
				g_have_rendered = false;
			}

			const int x = client.right - width - Px(kMarginRight);
			const int y = client.top + Px(kMarginTop);

			const LONG state_a = ReadState(g_state_a);
			const LONG state_b = ReadState(g_state_b);
			const ULONGLONG now = GetTickCount64();
			if (state_a != g_last_a || state_b != g_last_b)
			{
				g_last_a = state_a;
				g_last_b = state_b;
				g_last_change_tick = now;
			}
			const int alpha = (now - g_last_change_tick) < kHighlightMs ? kHighlightAlpha : kBackgroundAlpha;

			if (g_have_rendered && g_rendered_a == state_a && g_rendered_b == state_b &&
				g_rendered_x == x && g_rendered_y == y &&
				g_rendered_width == width && g_rendered_height == height &&
				g_rendered_alpha == alpha)
			{
				return;	// 位置和内容都没变, 不用重画
			}

			RenderPanel(width, height, state_a != 0, state_b != 0, alpha);

			POINT destination{ x, y };
			SIZE size{ width, height };
			POINT source{ 0, 0 };
			BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

			if (!g_panel_shown)
			{
				ShowWindow(g_panel_window, SW_SHOWNOACTIVATE);
				g_panel_shown = 1;
			}

			SetWindowPos(
				g_panel_window, HWND_TOPMOST, x, y, width, height,
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW
			);
			UpdateLayeredWindow(g_panel_window, nullptr, &destination, &size, g_panel_surface.dc, &source, 0, &blend, ULW_ALPHA);

			g_have_rendered = true;
			g_rendered_a = state_a;
			g_rendered_b = state_b;
			g_rendered_x = x;
			g_rendered_y = y;
			g_rendered_width = width;
			g_rendered_height = height;
			g_rendered_alpha = alpha;
		}

		LRESULT CALLBACK PanelWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
		{
			switch (message)
			{
			case WM_TIMER:
				if (wparam == kTimerId)
				{
					UpdatePanel();
					return 0;
				}
				break;
			case WM_MOUSEACTIVATE:
				return MA_NOACTIVATE;
			case WM_NCHITTEST:
				return HTTRANSPARENT;
			case WM_ERASEBKGND:
				return 1;
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				break;
			}
			return DefWindowProcW(window, message, wparam, lparam);
		}

		DWORD WINAPI PanelThread(LPVOID)
		{
			WNDCLASSEXW window_class{};
			window_class.cbSize = sizeof(WNDCLASSEXW);
			window_class.lpfnWndProc = PanelWindowProc;
			window_class.hInstance = g_instance;
			window_class.lpszClassName = kWindowClass;

			if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			{
				LogMessage("overlay: RegisterClassExW failed, no status panel");
				return 0;
			}

			g_panel_window = CreateWindowExW(
				WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
				kWindowClass,
				L"ChroniconMapReveal status",
				WS_POPUP,
				0, 0, 16, 16,
				nullptr,
				nullptr,
				g_instance,
				nullptr
			);

			if (!g_panel_window)
			{
				LogMessage("overlay: CreateWindowExW failed, no status panel");
				return 0;
			}

			SetTimer(g_panel_window, kTimerId, kRefreshMs, nullptr);
			LogMessage("overlay: status panel thread running");

			MSG message;
			while (GetMessageW(&message, nullptr, 0, 0) > 0)
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}

			KillTimer(g_panel_window, kTimerId);
			return 0;
		}
	}

	void Start(const volatile long* mechanism_a, const volatile long* mechanism_b, void (*log)(const char* message))
	{
		g_state_a = mechanism_a;
		g_state_b = mechanism_b;
		g_log = log;

		if (InterlockedCompareExchange(&g_started, 1, 0) != 0)
			return;

		g_instance = GetModuleHandleW(nullptr);

		HANDLE thread = CreateThread(nullptr, 0, PanelThread, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
		else
			LogMessage("overlay: CreateThread failed, no status panel");
	}

	void ToggleVisible()
	{
		const LONG visible = InterlockedCompareExchange(&g_user_visible, 0, 0);
		InterlockedExchange(&g_user_visible, visible ? 0 : 1);
		LogMessage(visible ? "overlay: status panel hidden by hotkey" : "overlay: status panel shown by hotkey");
	}
}

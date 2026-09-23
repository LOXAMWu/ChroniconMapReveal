// ChroniconMapReveal —— 可改键的热键模块 + 设置文件 (ini)
//
// * 低层键盘钩子 (WH_KEYBOARD_LL) 跑在自己的消息循环线程上; 小键盘数字键按扫描码匹配,
//   所以 NumLock 开/关都能用, 而且只在游戏窗口处于前台时才响应。
// * 钩子只把按键记录成 "动作待处理" 标志, 真正的开关动作由游戏线程执行 —— 绘制判定补丁
//   会改写代码字节, 不能和可能正在执行那条指令的线程抢。
// * 键位可以在游戏里实时改 (按 rebind 键), 也可以直接编辑 ini 文件, 改动后自动重新加载。

#include "hotkeys.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace mr_hotkeys
{
	namespace
	{
		const wchar_t* const kIniSectionKeys = L"keys";
		const wchar_t* const kIniSectionAuto = L"auto";

		// ini 里的键名 / 默认键位 (顺序与 Action 一致)
		const wchar_t* const kIniKeyNames[ACTION_COUNT] =
		{
			L"pulse", L"draw_gate", L"status", L"panel", L"rebind"
		};
		const wchar_t* const kDefaultKeys[ACTION_COUNT] =
		{
			L"NUMPAD1", L"NUMPAD2", L"NUMPAD3", L"NUMPAD0", L"NUMPAD9"
		};

		// ------------------------------------------------------------ 键表

		struct KeyInfo
		{
			std::wstring name;		// ini / 日志写法
			std::wstring display;	// 面板写法
			int vk = 0;
			int scan = 0;			// 非 0 = 用扫描码匹配 (小键盘数字键)
		};

		void AddKey(std::vector<KeyInfo>& table, const wchar_t* name, const wchar_t* display, int vk, int scan = 0)
		{
			KeyInfo info;
			info.name = name;
			info.display = display;
			info.vk = vk;
			info.scan = scan;
			table.push_back(info);
		}

		std::vector<KeyInfo> BuildKeyTable()
		{
			std::vector<KeyInfo> table;

			// 小键盘数字键: NumLock 关闭时 VK 会变成 Insert/End/Down/PageDown,
			// 但扫描码不变, 所以这十一个键用扫描码匹配
			const wchar_t* numpad_names[10] =
			{
				L"NUMPAD0", L"NUMPAD1", L"NUMPAD2", L"NUMPAD3", L"NUMPAD4",
				L"NUMPAD5", L"NUMPAD6", L"NUMPAD7", L"NUMPAD8", L"NUMPAD9"
			};
			const wchar_t* numpad_display[10] =
			{
				L"小键盘0", L"小键盘1", L"小键盘2", L"小键盘3", L"小键盘4",
				L"小键盘5", L"小键盘6", L"小键盘7", L"小键盘8", L"小键盘9"
			};
			const int numpad_scan[10] = { 0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49 };
			for (int i = 0; i < 10; i++)
				AddKey(table, numpad_names[i], numpad_display[i], VK_NUMPAD0 + i, numpad_scan[i]);
			AddKey(table, L"NUMPADDECIMAL", L"小键盘.", VK_DECIMAL, 0x53);

			// 小键盘运算符: 不受 NumLock 影响, 直接用 VK 匹配
			AddKey(table, L"NUMPADDIVIDE", L"小键盘/", VK_DIVIDE);
			AddKey(table, L"NUMPADMULTIPLY", L"小键盘*", VK_MULTIPLY);
			AddKey(table, L"NUMPADSUBTRACT", L"小键盘-", VK_SUBTRACT);
			AddKey(table, L"NUMPADADD", L"小键盘+", VK_ADD);

			// 功能键
			for (int i = 1; i <= 24; i++)
			{
				wchar_t text[8] = L"";
				_snwprintf(text, 8, L"F%d", i);
				AddKey(table, text, text, VK_F1 + i - 1);
			}

			// 字母 / 数字
			for (wchar_t c = L'A'; c <= L'Z'; c++)
			{
				wchar_t text[2] = { c, 0 };
				AddKey(table, text, text, (int)c);
			}
			for (wchar_t c = L'0'; c <= L'9'; c++)
			{
				wchar_t text[2] = { c, 0 };
				AddKey(table, text, text, (int)c);
			}

			// 常用键
			AddKey(table, L"SPACE", L"空格", VK_SPACE);
			AddKey(table, L"TAB", L"Tab", VK_TAB);
			AddKey(table, L"ENTER", L"回车", VK_RETURN);
			AddKey(table, L"ESCAPE", L"Esc", VK_ESCAPE);
			AddKey(table, L"BACKSPACE", L"退格", VK_BACK);
			AddKey(table, L"INSERT", L"Insert", VK_INSERT);
			AddKey(table, L"DELETE", L"Delete", VK_DELETE);
			AddKey(table, L"HOME", L"Home", VK_HOME);
			AddKey(table, L"END", L"End", VK_END);
			AddKey(table, L"PAGEUP", L"PageUp", VK_PRIOR);
			AddKey(table, L"PAGEDOWN", L"PageDown", VK_NEXT);
			AddKey(table, L"UP", L"↑", VK_UP);
			AddKey(table, L"DOWN", L"↓", VK_DOWN);
			AddKey(table, L"LEFT", L"←", VK_LEFT);
			AddKey(table, L"RIGHT", L"→", VK_RIGHT);
			AddKey(table, L"MINUS", L"-", VK_OEM_MINUS);
			AddKey(table, L"EQUAL", L"=", VK_OEM_PLUS);
			AddKey(table, L"COMMA", L",", VK_OEM_COMMA);
			AddKey(table, L"PERIOD", L".", VK_OEM_PERIOD);
			AddKey(table, L"SLASH", L"/", VK_OEM_2);
			AddKey(table, L"SEMICOLON", L";", VK_OEM_1);
			AddKey(table, L"QUOTE", L"'", VK_OEM_7);
			AddKey(table, L"BACKQUOTE", L"`", VK_OEM_3);
			AddKey(table, L"BRACKETLEFT", L"[", VK_OEM_4);
			AddKey(table, L"BRACKETRIGHT", L"]", VK_OEM_6);
			AddKey(table, L"BACKSLASH", L"\\", VK_OEM_5);
			AddKey(table, L"CAPSLOCK", L"CapsLock", VK_CAPITAL);
			AddKey(table, L"NUMLOCK", L"NumLock", VK_NUMLOCK);
			AddKey(table, L"SCROLLLOCK", L"ScrollLock", VK_SCROLL);
			AddKey(table, L"PRINTSCREEN", L"PrtSc", VK_SNAPSHOT);
			AddKey(table, L"PAUSE", L"Pause", VK_PAUSE);
			AddKey(table, L"APPS", L"Menu", VK_APPS);

			// 修饰键本身也可以当主键
			AddKey(table, L"CTRL", L"Ctrl", VK_CONTROL);
			AddKey(table, L"SHIFT", L"Shift", VK_SHIFT);
			AddKey(table, L"ALT", L"Alt", VK_MENU);
			AddKey(table, L"WIN", L"Win", VK_LWIN);

			return table;
		}

		const std::vector<KeyInfo>& KeyTable()
		{
			static const std::vector<KeyInfo> table = BuildKeyTable();
			return table;
		}

		std::wstring UpperTrim(const std::wstring& text)
		{
			std::wstring out;
			out.reserve(text.size());
			for (wchar_t c : text)
			{
				if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')
					continue;
				out.push_back((wchar_t)towupper(c));
			}
			return out;
		}

		std::wstring CanonicalKeyName(const std::wstring& name)
		{
			if (name == L"RETURN") return L"ENTER";
			if (name == L"ESC") return L"ESCAPE";
			if (name == L"CONTROL" || name == L"LCTRL" || name == L"RCTRL") return L"CTRL";
			if (name == L"LSHIFT" || name == L"RSHIFT") return L"SHIFT";
			if (name == L"LALT" || name == L"RALT") return L"ALT";
			if (name == L"LWIN" || name == L"RWIN") return L"WIN";
			if (name == L"PGUP") return L"PAGEUP";
			if (name == L"PGDN") return L"PAGEDOWN";
			if (name == L"INS") return L"INSERT";
			if (name == L"DEL") return L"DELETE";
			if (name == L"TILDE" || name == L"GRAVE") return L"BACKQUOTE";
			if (name == L"NUMPADPLUS") return L"NUMPADADD";
			if (name == L"NUMPADMINUS") return L"NUMPADSUBTRACT";
			if (name == L"NUMPADDOT") return L"NUMPADDECIMAL";
			if (name == L"NUMPADSLASH") return L"NUMPADDIVIDE";
			if (name == L"NUMPADSTAR") return L"NUMPADMULTIPLY";
			if (name == L"MENU") return L"APPS";
			return name;
		}

		bool FindKeyByName(const std::wstring& name, KeyInfo& out)
		{
			const std::wstring wanted = CanonicalKeyName(UpperTrim(name));
			if (wanted.empty())
				return false;

			for (const KeyInfo& info : KeyTable())
			{
				if (info.name == wanted)
				{
					out = info;
					return true;
				}
			}
			return false;
		}

		bool FindKeyByVk(int vk, KeyInfo& out)
		{
			for (const KeyInfo& info : KeyTable())
			{
				if (info.scan == 0 && info.vk == vk)
				{
					out = info;
					return true;
				}
			}
			return false;
		}

		bool FindKeyByScan(int scan, KeyInfo& out)
		{
			for (const KeyInfo& info : KeyTable())
			{
				if (info.scan != 0 && info.scan == scan)
				{
					out = info;
					return true;
				}
			}
			return false;
		}

		int NormalizeVk(int vk)
		{
			switch (vk)
			{
			case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
			case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
			case VK_LMENU: case VK_RMENU: return VK_MENU;
			case VK_RWIN: return VK_LWIN;
			default: return vk;
			}
		}

		bool IsModifierVk(int vk)
		{
			return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN;
		}

		unsigned CurrentModifiers()
		{
			unsigned mods = 0;
			if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MR_MOD_CTRL;
			if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= MR_MOD_ALT;
			if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= MR_MOD_SHIFT;
			if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) mods |= MR_MOD_WIN;
			return mods;
		}

		std::wstring ModifierPrefix(unsigned mods)
		{
			std::wstring text;
			if (mods & MR_MOD_CTRL) text += L"Ctrl+";
			if (mods & MR_MOD_ALT) text += L"Alt+";
			if (mods & MR_MOD_SHIFT) text += L"Shift+";
			if (mods & MR_MOD_WIN) text += L"Win+";
			return text;
		}

		bool DescribeKey(const KeySpec& spec, KeyInfo& info)
		{
			if (spec.use_scan)
				return FindKeyByScan(spec.scan, info);
			return FindKeyByVk(spec.vk, info);
		}

		// 面板显示名 (中文)
		std::wstring FormatKeyDisplay(const KeySpec& spec)
		{
			if (!spec.valid)
				return L"未设置";

			KeyInfo info;
			const std::wstring base = DescribeKey(spec, info) ? info.display : L"未知键";
			return ModifierPrefix(spec.mods) + base;
		}

		// ini 写法 (ASCII)
		std::wstring FormatKeyName(const KeySpec& spec)
		{
			if (!spec.valid)
				return L"";

			KeyInfo info;
			const std::wstring base = DescribeKey(spec, info) ? info.name : L"";
			if (base.empty())
				return L"";

			std::wstring text;
			if (spec.mods & MR_MOD_CTRL) text += L"CTRL+";
			if (spec.mods & MR_MOD_ALT) text += L"ALT+";
			if (spec.mods & MR_MOD_SHIFT) text += L"SHIFT+";
			if (spec.mods & MR_MOD_WIN) text += L"WIN+";
			return text + base;
		}

		// 解析 "Ctrl+Shift+Numpad1" 这类写法
		bool ParseKeySpec(const std::wstring& text, KeySpec& out)
		{
			std::vector<std::wstring> parts;
			std::wstring current;
			for (wchar_t c : text)
			{
				if (c == L'+') { parts.push_back(current); current.clear(); }
				else current.push_back(c);
			}
			parts.push_back(current);

			if (parts.empty() || UpperTrim(parts.back()).empty())
				return false;

			KeySpec spec;
			for (size_t i = 0; i + 1 < parts.size(); i++)
			{
				const std::wstring modifier = UpperTrim(parts[i]);
				if (modifier == L"CTRL" || modifier == L"CONTROL" || modifier == L"LCTRL" || modifier == L"RCTRL")
					spec.mods |= MR_MOD_CTRL;
				else if (modifier == L"ALT" || modifier == L"LALT" || modifier == L"RALT")
					spec.mods |= MR_MOD_ALT;
				else if (modifier == L"SHIFT" || modifier == L"LSHIFT" || modifier == L"RSHIFT")
					spec.mods |= MR_MOD_SHIFT;
				else if (modifier == L"WIN" || modifier == L"LWIN" || modifier == L"RWIN")
					spec.mods |= MR_MOD_WIN;
				else
					return false;
			}

			const std::wstring key_name = UpperTrim(parts.back());
			KeyInfo info;
			if (key_name == L"CTRL" || key_name == L"CONTROL")
			{
				spec.vk = VK_CONTROL;
				spec.mods = 0;	// 修饰键本身当主键时不再要求修饰位
			}
			else if (key_name == L"SHIFT")
			{
				spec.vk = VK_SHIFT;
				spec.mods = 0;
			}
			else if (key_name == L"ALT")
			{
				spec.vk = VK_MENU;
				spec.mods = 0;
			}
			else if (key_name == L"WIN")
			{
				spec.vk = VK_LWIN;
				spec.mods = 0;
			}
			else if (FindKeyByName(key_name, info))
			{
				spec.vk = info.vk;
				spec.scan = info.scan;
				spec.use_scan = info.scan != 0;
			}
			else
			{
				return false;
			}

			spec.valid = true;
			out = spec;
			return true;
		}

		bool IsNumpadScan(unsigned scan)
		{
			return scan >= 0x47 && scan <= 0x53;
		}

		bool MakeSpecFromEvent(const KBDLLHOOKSTRUCT& key, KeySpec& out)
		{
			KeySpec spec;
			spec.mods = CurrentModifiers();

			if ((key.flags & LLKHF_EXTENDED) == 0 && IsNumpadScan(key.scanCode))
			{
				KeyInfo info;
				if (FindKeyByScan((int)key.scanCode, info))
				{
					spec.vk = info.vk;
					spec.scan = info.scan;
					spec.use_scan = true;
					spec.valid = true;
					out = spec;
					return true;
				}
				// 小键盘 +/- 这类键没有扫描码条目, 退回 VK 匹配
			}

			spec.vk = NormalizeVk((int)key.vkCode);
			spec.valid = true;
			out = spec;
			return true;
		}

		bool Matches(const KeySpec& binding, const KBDLLHOOKSTRUCT& key, unsigned mods)
		{
			if (!binding.valid)
				return false;

			if (binding.mods != mods && !(binding.mods == 0 && IsModifierVk(binding.vk)))
				return false;

			if (binding.use_scan)
				return (key.flags & LLKHF_EXTENDED) == 0 && (int)key.scanCode == binding.scan;

			return NormalizeVk((int)key.vkCode) == binding.vk;
		}

		bool SameKey(const KeySpec& a, const KeySpec& b)
		{
			if (!a.valid || !b.valid)
				return false;
			if (a.vk != b.vk || a.use_scan != b.use_scan || a.mods != b.mods)
				return false;
			if (a.use_scan && a.scan != b.scan)
				return false;
			return true;
		}

		// ------------------------------------------------------------ ini 文件

		std::string TrimAscii(const std::string& text)
		{
			size_t begin = 0;
			size_t end = text.size();
			while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
				begin++;
			while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
				end--;
			return text.substr(begin, end - begin);
		}

		bool EqualsNoCase(const std::string& a, const std::string& b)
		{
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); i++)
			{
				const char left = (char)tolower((unsigned char)a[i]);
				const char right = (char)tolower((unsigned char)b[i]);
				if (left != right)
					return false;
			}
			return true;
		}

		std::wstring WidenAscii(const std::string& text)
		{
			std::wstring out;
			out.reserve(text.size());
			for (char c : text)
				out.push_back((wchar_t)(unsigned char)c);
			return out;
		}

		std::string NarrowAscii(const std::wstring& text)
		{
			std::string out;
			out.reserve(text.size());
			for (wchar_t c : text)
				out.push_back(c < 0x80 ? (char)c : '?');
			return out;
		}

		std::string SectionOf(const std::string& line)
		{
			if (line.empty() || line[0] != '[')
				return std::string();
			const size_t close = line.find(']');
			if (close == std::string::npos)
				return std::string();
			return TrimAscii(line.substr(1, close - 1));
		}

		struct IniDocument
		{
			std::vector<std::string> lines;

			int FindIndex(const std::string& section, const std::string& key) const
			{
				std::string current;
				for (int i = 0; i < (int)lines.size(); i++)
				{
					const std::string line = TrimAscii(lines[i]);
					if (line.empty() || line[0] == ';' || line[0] == '#')
						continue;

					if (line[0] == '[')
					{
						current = SectionOf(line);
						continue;
					}

					if (!EqualsNoCase(current, section))
						continue;

					const size_t equals = line.find('=');
					if (equals == std::string::npos)
						continue;
					if (EqualsNoCase(TrimAscii(line.substr(0, equals)), key))
						return i;
				}
				return -1;
			}

			std::string Get(const std::string& section, const std::string& key, const std::string& fallback) const
			{
				const int index = FindIndex(section, key);
				if (index < 0)
					return fallback;
				const std::string line = TrimAscii(lines[index]);
				const size_t equals = line.find('=');
				return TrimAscii(line.substr(equals + 1));
			}

			void Set(const std::string& section, const std::string& key, const std::string& value)
			{
				const std::string entry = key + "=" + value;

				const int existing = FindIndex(section, key);
				if (existing >= 0)
				{
					lines[existing] = entry;
					return;
				}

				int section_start = -1;
				int insert_at = (int)lines.size();
				for (int i = 0; i < (int)lines.size(); i++)
				{
					const std::string line = TrimAscii(lines[i]);
					if (line.empty() || line[0] != '[')
						continue;

					if (EqualsNoCase(SectionOf(line), section))
						section_start = i;
					else if (section_start >= 0)
					{
						insert_at = i;
						break;
					}
				}

				if (section_start >= 0)
				{
					lines.insert(lines.begin() + insert_at, entry);
					return;
				}

				if (!lines.empty() && !TrimAscii(lines.back()).empty())
					lines.push_back(std::string());
				lines.push_back("[" + section + "]");
				lines.push_back(entry);
			}
		};

		bool LoadIni(const std::wstring& path, IniDocument& document)
		{
			document.lines.clear();

			FILE* file = _wfopen(path.c_str(), L"rb");
			if (!file)
				return false;

			std::string data;
			char buffer[4096];
			size_t read = 0;
			while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
				data.append(buffer, read);
			fclose(file);

			if (data.size() >= 3 &&
				(unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
			{
				data.erase(0, 3);
			}

			std::string line;
			for (char c : data)
			{
				if (c == '\n')
				{
					if (!line.empty() && line.back() == '\r')
						line.pop_back();
					document.lines.push_back(line);
					line.clear();
				}
				else
				{
					line.push_back(c);
				}
			}
			if (!line.empty())
				document.lines.push_back(line);

			return true;
		}

		bool SaveIni(const std::wstring& path, const IniDocument& document)
		{
			FILE* file = _wfopen(path.c_str(), L"wb");
			if (!file)
				return false;

			for (const std::string& line : document.lines)
			{
				fwrite(line.c_str(), 1, line.size(), file);
				fwrite("\r\n", 1, 2, file);
			}
			fclose(file);
			return true;
		}

		// 首次运行时写一份带注释的模板 (UTF-8)
		void EnsureConfigFile(const std::wstring& path)
		{
			if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
				return;

			FILE* file = _wfopen(path.c_str(), L"wb");
			if (!file)
				return;

			const char* text =
				"; ChroniconMapReveal 设置文件 (UTF-8)\r\n"
				";\r\n"
				"; 键名写法: NUMPAD1 / F5 / A / SPACE / CTRL+F1 ... (大小写随意)\r\n"
				"; 可以直接改这个文件, 游戏里最多 2 秒后自动生效;\r\n"
				"; 也可以在游戏里按 rebind 键 (默认 NUMPAD9) 依次给每个动作改键。\r\n"
				"\r\n"
				"[keys]\r\n"
				"; 手动触发一次机制 A (进区域时的自动脉冲见 [auto])\r\n"
				"pulse=NUMPAD1\r\n"
				"; 开关机制 B (绘制判定补丁, 默认常开)\r\n"
				"draw_gate=NUMPAD2\r\n"
				"; 把运行状态输出到 aurie.log\r\n"
				"status=NUMPAD3\r\n"
				"; 显示 / 隐藏右上角状态面板\r\n"
				"panel=NUMPAD0\r\n"
				"; 进入改键模式 (每按一次改下一个动作)\r\n"
				"rebind=NUMPAD9\r\n"
				"\r\n"
				"[auto]\r\n"
				"; 进入区域后自动脉冲机制 A\r\n"
				"enabled=1\r\n"
				"; 进入区域多少毫秒后开启\r\n"
				"delay_ms=1000\r\n"
				"; 开启持续多少毫秒\r\n"
				"hold_ms=500\r\n";

			fwrite(text, 1, strlen(text), file);
			fclose(file);
		}

		// ------------------------------------------------------------ 状态

		std::wstring g_ini_path;
		KeySpec g_bindings[ACTION_COUNT];
		CRITICAL_SECTION g_lock;
		bool g_lock_ready = false;
		volatile LONG g_started = 0;
		volatile LONG g_action_pending[ACTION_COUNT] = { 0, 0, 0, 0, 0 };
		volatile LONG g_capturing = 0;
		volatile LONG g_captured_pending = 0;
		KeySpec g_captured_key;
		volatile LONG g_binding_version = 0;
		bool g_key_down[256] = { false };		// 只由钩子线程读写
		ULONGLONG g_config_hash = 0;
		ULONGLONG g_last_config_check = 0;
		bool g_auto_enabled = true;
		int g_auto_delay_ms = 1000;
		int g_auto_hold_ms = 500;

		int ClampInt(int value, int low, int high)
		{
			if (value < low) return low;
			if (value > high) return high;
			return value;
		}

		// 文件内容哈希 (FNV-1a)。0 = 读不到文件 —— 用内容而不是时间戳判断,
		// 因为同一个系统时钟 tick 内写入两次时, 文件时间戳可能完全一样。
		ULONGLONG HashConfigFile(const std::wstring& path)
		{
			FILE* file = _wfopen(path.c_str(), L"rb");
			if (!file)
				return 0;

			ULONGLONG hash = 1469598103934665603ULL;
			unsigned char buffer[512];
			size_t read = 0;
			while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
			{
				for (size_t i = 0; i < read; i++)
				{
					hash ^= buffer[i];
					hash *= 1099511628211ULL;
				}
			}
			fclose(file);
			return hash == 0 ? 1 : hash;
		}

		void ApplyDefaultBindings()
		{
			for (int i = 0; i < ACTION_COUNT; i++)
			{
				g_bindings[i] = KeySpec{};
				KeyInfo info;
				if (!FindKeyByName(kDefaultKeys[i], info))
					continue;
				g_bindings[i].vk = info.vk;
				g_bindings[i].scan = info.scan;
				g_bindings[i].use_scan = info.scan != 0;
				g_bindings[i].valid = true;
			}
		}

		void LoadConfig()
		{
			ApplyDefaultBindings();
			g_auto_enabled = true;
			g_auto_delay_ms = 1000;
			g_auto_hold_ms = 500;

			if (g_ini_path.empty())
			{
				InterlockedIncrement(&g_binding_version);
				return;
			}

			EnsureConfigFile(g_ini_path);

			IniDocument document;
			LoadIni(g_ini_path, document);

			if (g_lock_ready)
			{
				EnterCriticalSection(&g_lock);
				for (int i = 0; i < ACTION_COUNT; i++)
				{
					const std::string value = document.Get("keys", NarrowAscii(kIniKeyNames[i]), std::string());
					if (value.empty())
						continue;

					KeySpec spec;
					if (ParseKeySpec(WidenAscii(value), spec))
						g_bindings[i] = spec;
				}
				LeaveCriticalSection(&g_lock);
			}

			g_auto_enabled = document.Get("auto", "enabled", "1") != "0";
			g_auto_delay_ms = ClampInt(atoi(document.Get("auto", "delay_ms", "1000").c_str()), 0, 60000);
			g_auto_hold_ms = ClampInt(atoi(document.Get("auto", "hold_ms", "500").c_str()), 0, 60000);

			g_config_hash = HashConfigFile(g_ini_path);
			InterlockedIncrement(&g_binding_version);
		}

		void SaveBindingsToIni()
		{
			if (g_ini_path.empty())
				return;

			EnsureConfigFile(g_ini_path);

			IniDocument document;
			LoadIni(g_ini_path, document);

			if (g_lock_ready)
			{
				EnterCriticalSection(&g_lock);
				for (int i = 0; i < ACTION_COUNT; i++)
					document.Set("keys", NarrowAscii(kIniKeyNames[i]), NarrowAscii(FormatKeyName(g_bindings[i])));
				LeaveCriticalSection(&g_lock);
			}

			SaveIni(g_ini_path, document);
			g_config_hash = HashConfigFile(g_ini_path);
		}

		// ------------------------------------------------------------ 键盘钩子

		bool GameIsForeground()
		{
			HWND foreground = GetForegroundWindow();
			if (!foreground)
				return false;

			DWORD process_id = 0;
			GetWindowThreadProcessId(foreground, &process_id);
			return process_id == GetCurrentProcessId();
		}

		void DispatchKeyDown(const KBDLLHOOKSTRUCT& key)
		{
			if (!GameIsForeground())
				return;

			// 改键模式: 下一个按键就是新键位
			if (InterlockedCompareExchange(&g_capturing, 0, 0) != 0)
			{
				KeySpec spec;
				if (MakeSpecFromEvent(key, spec))
				{
					if (g_lock_ready)
					{
						EnterCriticalSection(&g_lock);
						g_captured_key = spec;
						LeaveCriticalSection(&g_lock);
					}
					InterlockedExchange(&g_captured_pending, 1);
					InterlockedExchange(&g_capturing, 0);
				}
				return;
			}

			const unsigned mods = CurrentModifiers();

			KeySpec local[ACTION_COUNT];
			for (int i = 0; i < ACTION_COUNT; i++)
				local[i] = KeySpec{};

			if (g_lock_ready)
			{
				EnterCriticalSection(&g_lock);
				for (int i = 0; i < ACTION_COUNT; i++)
					local[i] = g_bindings[i];
				LeaveCriticalSection(&g_lock);
			}

			for (int i = 0; i < ACTION_COUNT; i++)
			{
				if (Matches(local[i], key, mods))
					InterlockedExchange(&g_action_pending[i], 1);
			}
		}

		LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wparam, LPARAM lparam)
		{
			if (code == HC_ACTION)
			{
				const KBDLLHOOKSTRUCT& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
				const int vk = (int)key.vkCode & 0xFF;

				if (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)
				{
					if (!g_key_down[vk])	// 忽略长按产生的重复
					{
						g_key_down[vk] = true;
						DispatchKeyDown(key);
					}
				}
				else if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP)
				{
					g_key_down[vk] = false;
				}
			}
			return CallNextHookEx(nullptr, code, wparam, lparam);
		}

		DWORD WINAPI KeyboardHookThread(LPVOID)
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&LowLevelKeyboardProc),
					&module))
			{
				module = GetModuleHandleW(nullptr);
			}

			HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, module, 0);

			MSG message;
			while (GetMessageW(&message, nullptr, 0, 0) > 0)
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}

			if (hook)
				UnhookWindowsHookEx(hook);
			return 0;
		}
	}

	// ---------------------------------------------------------------- 对外接口

	void Start(const std::wstring& ini_path)
	{
		if (InterlockedCompareExchange(&g_started, 1, 0) != 0)
			return;		// 已经启动过了

		InitializeCriticalSection(&g_lock);
		g_lock_ready = true;
		g_ini_path = ini_path;

		LoadConfig();
		g_config_hash = HashConfigFile(g_ini_path);

		HANDLE thread = CreateThread(nullptr, 0, KeyboardHookThread, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);

		InterlockedIncrement(&g_binding_version);
	}

	bool TakeAction(Action action)
	{
		if (action < 0 || action >= ACTION_COUNT)
			return false;
		return InterlockedExchange(&g_action_pending[action], 0) != 0;
	}

	void CaptureNextKey(bool enable)
	{
		if (!enable)
			InterlockedExchange(&g_captured_pending, 0);
		InterlockedExchange(&g_capturing, enable ? 1 : 0);
	}

	bool IsCapturing()
	{
		return InterlockedCompareExchange(&g_capturing, 0, 0) != 0;
	}

	bool TakeCapturedKey(KeySpec& key)
	{
		if (InterlockedExchange(&g_captured_pending, 0) == 0)
			return false;

		if (g_lock_ready)
		{
			EnterCriticalSection(&g_lock);
			key = g_captured_key;
			LeaveCriticalSection(&g_lock);
		}
		return true;
	}

	bool SetBinding(Action action, const KeySpec& key)
	{
		if (action < 0 || action >= ACTION_COUNT || !key.valid || !g_lock_ready)
			return false;

		EnterCriticalSection(&g_lock);
		for (int i = 0; i < ACTION_COUNT; i++)
		{
			if (i == (int)action)
				continue;
			if (SameKey(g_bindings[i], key))
			{
				LeaveCriticalSection(&g_lock);
				return false;	// 这个键已经被别的动作占用了
			}
		}
		g_bindings[action] = key;
		LeaveCriticalSection(&g_lock);

		InterlockedIncrement(&g_binding_version);
		SaveBindingsToIni();
		return true;
	}

	void PollConfigFile()
	{
		if (g_ini_path.empty())
			return;

		const ULONGLONG now = GetTickCount64();
		if (now - g_last_config_check < 2000)
			return;
		g_last_config_check = now;

		EnsureConfigFile(g_ini_path);	// 文件被删掉就重新生成模板
		const ULONGLONG hash = HashConfigFile(g_ini_path);
		if (hash == 0 || hash == g_config_hash)
			return;

		LoadConfig();	// 内部会刷新 g_config_hash
	}

	std::wstring BindingText(Action action)
	{
		if (action < 0 || action >= ACTION_COUNT)
			return L"";

		KeySpec spec;
		if (g_lock_ready)
		{
			EnterCriticalSection(&g_lock);
			spec = g_bindings[action];
			LeaveCriticalSection(&g_lock);
		}
		return FormatKeyDisplay(spec);
	}

	bool ParseKey(const std::wstring& text, KeySpec& key)
	{
		return ParseKeySpec(text, key);
	}

	std::wstring BindingName(Action action)
	{
		if (action < 0 || action >= ACTION_COUNT)
			return L"";

		KeySpec spec;
		if (g_lock_ready)
		{
			EnterCriticalSection(&g_lock);
			spec = g_bindings[action];
			LeaveCriticalSection(&g_lock);
		}
		return FormatKeyName(spec);
	}

	const wchar_t* ActionName(Action action)
	{
		switch (action)
		{
		case ACTION_PULSE: return L"手动全开地图";
		case ACTION_DRAW_GATE: return L"绘制判定补丁";
		case ACTION_STATUS: return L"输出状态";
		case ACTION_PANEL: return L"显示/隐藏面板";
		case ACTION_REBIND: return L"改键";
		default: return L"?";
		}
	}

	unsigned BindingVersion()
	{
		return (unsigned)InterlockedCompareExchange(&g_binding_version, 0, 0);
	}

	bool AutoPulseEnabled()
	{
		return g_auto_enabled;
	}

	int AutoPulseDelayMs()
	{
		return g_auto_delay_ms;
	}

	int AutoPulseHoldMs()
	{
		return g_auto_hold_ms;
	}
}

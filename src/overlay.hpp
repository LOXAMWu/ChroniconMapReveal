// ChroniconMapReveal —— 右上角状态面板 (overlay) 接口
//
// 面板由一个置顶的 layered 窗口绘制, 不依赖游戏使用的图形 API, 也不接收鼠标/键盘输入。
#pragma once

namespace mr_overlay
{
	// 启动后台线程, 在游戏窗口右上角绘制半透明状态面板。
	//   mechanism_a / mechanism_b: 指向 Mod 的开关标志 (LONG, 0 = 关闭)
	//   log: 可选的单行日志回调, 传 nullptr 表示不记录
	void Start(const volatile long* mechanism_a, const volatile long* mechanism_b, void (*log)(const char* message));

	// 显示 / 隐藏面板 (热键调用, 可在任意线程)。
	void ToggleVisible();
}

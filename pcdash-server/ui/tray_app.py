"""Windows 系统托盘应用 — pystray 实现"""

import threading
import logging

from PIL import Image, ImageDraw
import pystray

logger = logging.getLogger(__name__)


def _create_icon_image(color: str = "#00cc66") -> Image.Image:
    """生成一个简单的托盘图标（带 PC 监控标识的方块）"""
    size = 64
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # 外框
    draw.rounded_rectangle([4, 4, 60, 60], radius=8, fill=color, outline="#ffffff", width=2)
    # 绘制 "PC" 字样
    draw.text((14, 18), "PC", fill="#ffffff")
    # 底部小横线表示仪表盘
    draw.rectangle([16, 44, 48, 46], fill="#ffffff")
    draw.rectangle([16, 48, 32, 50], fill="#ffffff")
    return img


class TrayApp:
    def __init__(self, on_open_config=None, on_quit=None, get_status=None):
        self._on_open_config = on_open_config
        self._on_quit = on_quit
        self._get_status = get_status
        self._icon: pystray.Icon | None = None

    def _build_menu(self) -> pystray.Menu:
        """构建右键菜单"""
        items = []

        # 状态信息（只显示，不可点击）
        if self._get_status:
            status = self._get_status()
            items.append(pystray.MenuItem(
                f"HTTP: {status.get('http', '?')}",
                action=None, enabled=False
            ))
            items.append(pystray.MenuItem(
                f"串口: {status.get('serial', '?')}",
                action=None, enabled=False
            ))
            items.append(pystray.Menu.SEPARATOR)

        items.append(pystray.MenuItem("打开配置", self._on_config_click))
        items.append(pystray.Menu.SEPARATOR)
        items.append(pystray.MenuItem("退出", self._on_quit_click))
        return pystray.Menu(*items)

    def _on_config_click(self, icon, item):
        if self._on_open_config:
            t = threading.Thread(target=self._on_open_config, daemon=True)
            t.start()

    def _on_quit_click(self, icon, item):
        logger.info("用户请求退出")
        if self._icon:
            self._icon.stop()
        if self._on_quit:
            self._on_quit()

    def run(self):
        """启动托盘图标（阻塞当前线程）"""
        self._icon = pystray.Icon(
            name="PCDash",
            icon=_create_icon_image(),
            title="PCDash Monitor",
            menu=self._build_menu(),
        )
        logger.info("系统托盘已启动")
        self._icon.run()

    def run_in_thread(self) -> threading.Thread:
        """在后台线程启动托盘"""
        t = threading.Thread(target=self.run, daemon=True, name="tray-app")
        t.start()
        return t

    def stop(self):
        if self._icon:
            self._icon.stop()

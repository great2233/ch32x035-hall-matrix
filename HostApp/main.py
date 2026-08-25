#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
8x8 霍尔矩阵磁场强度可视化上位机
================================
通过 USB CDC 虚拟串口接收下位机 (CH32X035) 周期扫描的 8x8 霍尔矩阵数据，
以热力图 + 数值网格的方式实时显示磁场强度。

数据帧格式（下位机每次扫描后发送）:
    \r\n=== Hall Matrix (8x8) ===\r\n
    1234 1234 1234 1234 1234 1234 1234 1234\r\n   (共 8 行，每行 8 个 0-4095 值)
    ==========================\r\n

运行:
    py -3.12 main.py            # 图形界面
    py -3.12 main.py --selftest # 无界面，验证串口数据解析
"""

import sys
import re
import time
import argparse

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtGui import QPainter, QColor, QPen, QFont, QFontMetrics
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QComboBox, QPushButton, QLabel, QCheckBox, QLineEdit, QGroupBox,
    QTextEdit, QSplitter, QSizePolicy, QMessageBox,
)

import serial
from serial.tools import list_ports


# ---------------------------------------------------------------------------
# 串口协议解析
# ---------------------------------------------------------------------------

_START_MARK = b"=== Hall Matrix"
_END_LINE_RE = re.compile(rb"(?m)^=+\r?$")


def parse_matrix_frame(data: bytes) -> list | None:
    """从一段字节流中解析出第一个完整的 8x8 矩阵帧。

    找到 `=== Hall Matrix` 起始标记后，提取其后直到 `====` 分隔线之间的
    数字行，要求恰好 8 行且每行 8 个整数。

    返回 (矩阵 8x8 list, 结束索引)；数据不完整返回 None。
    """
    start = data.find(_START_MARK)
    if start < 0:
        return None

    end = None
    for m in _END_LINE_RE.finditer(data, start + len(_START_MARK)):
        if m.start() > start:
            end = m.start()
            break
    if end is None:
        return None

    body = data[start + len(_START_MARK):end]
    rows = []
    for line in body.split(b"\r\n"):
        line = line.strip()
        if not line:
            continue
        try:
            nums = [int(x) for x in line.split()]
        except ValueError:
            nums = []
        if len(nums) == 8:
            rows.append(nums)
            if len(rows) == 8:
                return rows, end

    # 数据行不足 8 行：不是完整帧，返回 None（等待更多数据）
    return None


def parse_frames(buffer: bytearray, max_frames: int = 8) -> list:
    """从缓冲中尽可能多地解析矩阵帧，返回帧列表。"""
    frames = []
    for _ in range(max_frames):
        data = bytes(buffer)
        start = data.find(_START_MARK)
        if start < 0:
            # 无完整起始标记：丢弃前部，保留可能跨块的尾部
            keep = len(_START_MARK) - 1
            if len(buffer) > keep:
                del buffer[:-keep]
            break
        result = parse_matrix_frame(data[start:])
        if result is None:
            # 帧未完整：清掉起始标记之前的杂讯，等待更多数据
            if start > 0:
                del buffer[:start]
            break
        matrix, consumed = result
        frames.append(matrix)
        del buffer[:start + consumed]
    return frames


def _parse_from_bytes(data: bytes) -> list | None:
    """供自测使用的独立解析接口：解析第一个完整帧。"""
    result = parse_matrix_frame(data)
    if result is None:
        return None
    matrix, _ = result
    return matrix


# ---------------------------------------------------------------------------
# 串口工作线程
# ---------------------------------------------------------------------------

class SerialThread(QThread):
    matrix_ready = Signal(object)        # 8x8 矩阵 (list[list[int]])
    status_changed = Signal(bool, str)   # 连接状态, 描述
    log = Signal(str)

    def __init__(self, port: str, baud: int = 115200, parent=None):
        super().__init__(parent)
        self.port = port
        self.baud = baud
        self._running = False
        self._buffer = bytearray()
        self.frame_count = 0

    def stop(self):
        self._running = False
        self.wait(2000)

    def send_command(self, cmd: str):
        if cmd:
            data = (cmd.rstrip() + "\r\n").encode("utf-8")
            try:
                self.ser.write(data)
            except Exception as exc:
                self.log.emit(f"发送失败: {exc}")

    def run(self):
        self._running = True
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.05)
        except Exception as exc:
            self.status_changed.emit(False, f"打开串口失败: {exc}")
            return
        self.status_changed.emit(True, f"已连接 {self.port} @ {self.baud}")

        while self._running:
            try:
                n = self.ser.in_waiting
                if n > 0:
                    self._buffer += self.ser.read(n)
                    frames = parse_frames(self._buffer)
                    for m in frames:
                        self.frame_count += 1
                        self.matrix_ready.emit(m)
                else:
                    self.msleep(10)
            except serial.SerialException as exc:
                self.log.emit(f"串口异常: {exc}")
                break
            except Exception as exc:
                self.log.emit(f"接收异常: {exc}")
                break

        try:
            self.ser.close()
        except Exception:
            pass
        self.status_changed.emit(False, f"已断开 {self.port}")


# ---------------------------------------------------------------------------
# 双色渐变（蓝 -> 红，按强度线性插值）
# ---------------------------------------------------------------------------

# 渐变端点颜色 (R, G, B)：低值颜色 / 高值颜色
_GRAD_LO = (0x1F, 0x63, 0xFF)   # 蓝
_GRAD_HI = (0xFF, 0x1F, 0x1F)   # 红


def _grad_rgb(t: float) -> tuple:
    """t 在 [0,1]，返回 (r, g, b)，0-255，两端颜色线性插值过渡。"""
    t = max(0.0, min(1.0, t))
    r = _GRAD_LO[0] + t * (_GRAD_HI[0] - _GRAD_LO[0])
    g = _GRAD_LO[1] + t * (_GRAD_HI[1] - _GRAD_LO[1])
    b = _GRAD_LO[2] + t * (_GRAD_HI[2] - _GRAD_LO[2])
    return int(round(r)), int(round(g)), int(round(b))


# ---------------------------------------------------------------------------
# 热力图控件
# ---------------------------------------------------------------------------

class HeatmapWidget(QWidget):
    """8x8 磁场强度热力图。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(320, 320)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.matrix = [[0] * 8 for _ in range(8)]
        self.auto_range = True
        self.vmin, self.vmax = 0.0, 4095.0
        self.show_value = True

    def set_matrix(self, matrix, auto_range: bool = True, vmin: float = 0, vmax: float = 4095):
        self.matrix = matrix
        self.auto_range = auto_range
        if auto_range:
            lo = min(min(row) for row in matrix)
            hi = max(max(row) for row in matrix)
            self.vmin, self.vmax = float(lo), float(hi)
        else:
            self.vmin, self.vmax = float(vmin), float(vmax)
        if self.vmax <= self.vmin:
            self.vmax = self.vmin + 1.0
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        margin = 6
        grid_w = w - 2 * margin
        grid_h = h - 2 * margin
        cell_w = grid_w / 8
        cell_h = grid_h / 8

        for r in range(8):
            for c in range(8):
                val = self.matrix[r][c]
                t = (val - self.vmin) / (self.vmax - self.vmin)
                r_, g_, b_ = _grad_rgb(t)
                color = QColor(r_, g_, b_)
                x = margin + c * cell_w
                y = margin + r * cell_h
                painter.fillRect(
                    int(x) + 1, int(y) + 1, int(cell_w) - 2, int(cell_h) - 2, color)

        painter.setPen(QPen(QColor(40, 40, 40), 1))
        painter.setFont(QFont("Consolas", 9))
        if self.show_value and cell_w > 30 and cell_h > 18:
            fm = QFontMetrics(painter.font())
            for r in range(8):
                for c in range(8):
                    txt = str(self.matrix[r][c])
                    tw = fm.horizontalAdvance(txt)
                    x = margin + c * cell_w + (cell_w - tw) / 2
                    y = margin + r * cell_h + cell_h / 2 + fm.ascent() / 2 - 1
                    painter.drawText(int(x), int(y), txt)

        # 网格线
        painter.setPen(QPen(QColor(20, 20, 20, 120), 1))
        for i in range(9):
            x = margin + i * cell_w
            y = margin + i * cell_h
            painter.drawLine(int(x), margin, int(x), int(h - margin))
            painter.drawLine(margin, int(y), int(w - margin), int(y))


class ColorBarWidget(QWidget):
    """竖直 Jet 色条 + 刻度。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedWidth(48)
        self.vmin, self.vmax = 0.0, 4095.0

    def set_range(self, vmin: float, vmax: float):
        self.vmin, self.vmax = float(vmin), float(vmax)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        w, h = self.width(), self.height()
        margin = 4
        top = margin
        bottom = h - margin
        bar_h = bottom - top
        n = 256
        for i in range(n):
            t = i / (n - 1)
            r_, g_, b_ = _grad_rgb(t)
            painter.setPen(QColor(r_, g_, b_))
            y = int(top + (1 - t) * bar_h)
            painter.drawLine(4, y, w - 10, y)

        painter.setPen(QPen(QColor(220, 220, 220)))
        painter.setFont(QFont("Consolas", 8))
        painter.drawText(8, top + 6, f"{self.vmax:.0f}")
        mid = (self.vmin + self.vmax) / 2
        painter.drawText(8, top + bar_h / 2 + 4, f"{mid:.0f}")
        painter.drawText(8, bottom - 2, f"{self.vmin:.0f}")


# ---------------------------------------------------------------------------
# 主窗口
# ---------------------------------------------------------------------------

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("8x8 霍尔矩阵 - 磁场强度可视化")
        self.resize(760, 640)

        self.thread = None
        self.frame_count = 0
        self.last_time = time.time()
        self.rate = 0.0
        self.last_matrix = [[0] * 8 for _ in range(8)]

        self._build_ui()
        self._refresh_ports()
        self._status_bar()

    # ---------- UI ----------
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)

        # 顶部：串口控制
        top = QHBoxLayout()
        top.addWidget(QLabel("串口:"))
        self.combo_ports = QComboBox()
        self.combo_ports.setMinimumWidth(140)
        top.addWidget(self.combo_ports)
        self.btn_refresh = QPushButton("刷新")
        self.btn_refresh.clicked.connect(self._refresh_ports)
        top.addWidget(self.btn_refresh)
        self.btn_connect = QPushButton("连接")
        self.btn_connect.clicked.connect(self._toggle_connect)
        top.addWidget(self.btn_connect)

        top.addSpacing(16)
        self.chk_auto_range = QCheckBox("自动范围")
        self.chk_auto_range.setChecked(True)
        self.chk_auto_range.toggled.connect(self._on_range_changed)
        top.addWidget(self.chk_auto_range)
        self.chk_show_value = QCheckBox("显示数值")
        self.chk_show_value.setChecked(True)
        self.chk_show_value.toggled.connect(self._on_range_changed)
        top.addWidget(self.chk_show_value)
        self.btn_save = QPushButton("保存CSV")
        self.btn_save.clicked.connect(self._save_csv)
        top.addWidget(self.btn_save)
        top.addStretch(1)
        root.addLayout(top)

        # 中部：热力图 + 色条 + 数值统计
        mid = QHBoxLayout()
        self.heatmap = HeatmapWidget()
        mid.addWidget(self.heatmap, 1)

        right = QVBoxLayout()
        self.colorbar = ColorBarWidget()
        right.addWidget(self.colorbar)
        self.lbl_range = QLabel("范围: --")
        self.lbl_range.setAlignment(Qt.AlignCenter)
        right.addWidget(self.lbl_range)
        right.addStretch(1)
        mid.addLayout(right)
        root.addLayout(mid, 1)

        # 底部：命令发送 + 日志
        cmd_row = QHBoxLayout()
        cmd_row.addWidget(QLabel("命令:"))
        self.edit_cmd = QLineEdit()
        self.edit_cmd.setPlaceholderText("例如: hall / status / help")
        self.edit_cmd.returnPressed.connect(self._send_cmd)
        cmd_row.addWidget(self.edit_cmd, 1)
        self.btn_send = QPushButton("发送")
        self.btn_send.clicked.connect(self._send_cmd)
        cmd_row.addWidget(self.btn_send)
        root.addLayout(cmd_row)

        self.log_view = QTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumHeight(120)
        root.addWidget(self.log_view)

    def _status_bar(self):
        self.lbl_state = QLabel("未连接")
        self.lbl_frame = QLabel("帧: 0")
        self.lbl_rate = QLabel("速率: 0.0 Hz")
        self.statusBar().addWidget(self.lbl_state)
        self.statusBar().addPermanentWidget(self.lbl_frame)
        self.statusBar().addPermanentWidget(self.lbl_rate)

    # ---------- 串口 ----------
    def _refresh_ports(self):
        current = self.combo_ports.currentText()
        self.combo_ports.clear()
        ports = [p.device for p in list_ports.comports()]
        if not ports:
            ports = ["无可用串口"]
        self.combo_ports.addItems(ports)
        if current in ports:
            self.combo_ports.setCurrentText(current)

    def _toggle_connect(self):
        if self.thread and self.thread.isRunning():
            self.thread.stop()
            self.thread = None
            self._set_connected(False)
        else:
            port = self.combo_ports.currentText()
            if port == "无可用串口":
                QMessageBox.warning(self, "提示", "没有可用串口，请先连接设备后刷新。")
                return
            self.thread = SerialThread(port, 115200, self)
            self.thread.matrix_ready.connect(self._on_matrix)
            self.thread.status_changed.connect(self._on_status)
            self.thread.log.connect(self._log)
            self.thread.start()

    def _on_status(self, ok: bool, msg: str):
        self._set_connected(ok)
        self._log(msg)
        if ok:
            self.frame_count = 0
            self.last_time = time.time()
            self.lbl_frame.setText("帧: 0")
            self.lbl_rate.setText("速率: 0.0 Hz")

    def _set_connected(self, ok: bool):
        self.btn_connect.setText("断开" if ok else "连接")
        self.btn_send.setEnabled(ok)
        self.edit_cmd.setEnabled(ok)
        self.lbl_state.setText("已连接" if ok else "未连接")
        self.lbl_state.setStyleSheet("color:#2e7d32;" if ok else "color:#c62828;")

    def _send_cmd(self):
        if self.thread and self.thread.isRunning():
            self.thread.send_command(self.edit_cmd.text())
            self.edit_cmd.clear()

    # ---------- 数据 ----------
    def _on_matrix(self, matrix):
        self.last_matrix = matrix
        self.frame_count += 1
        now = time.time()
        dt = now - self.last_time
        if dt >= 1.0:
            self.rate = self.frame_count / dt
            self.frame_count = 0
            self.last_time = now
            self.lbl_rate.setText(f"速率: {self.rate:.1f} Hz")
        self.lbl_frame.setText(f"帧: {self.frame_count}")

        self.heatmap.set_matrix(
            matrix,
            auto_range=self.chk_auto_range.isChecked(),
            vmin=0, vmax=4095,
        )
        self.colorbar.set_range(self.heatmap.vmin, self.heatmap.vmax)
        self.lbl_range.setText(
            f"范围: {self.heatmap.vmin:.0f} ~ {self.heatmap.vmax:.0f}")

    def _on_range_changed(self):
        if self.last_matrix:
            self._on_matrix(self.last_matrix)

    def _save_csv(self):
        import csv
        from datetime import datetime
        fn = f"hall_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        try:
            with open(fn, "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow(["行\\列"] + [f"列{c+1}" for c in range(8)])
                for r in range(8):
                    writer.writerow([f"行{r+1}"] + self.last_matrix[r])
            self._log(f"已保存: {fn}")
        except Exception as exc:
            self._log(f"保存失败: {exc}")

    def _log(self, msg: str):
        self.log_view.append(msg)
        self.log_view.verticalScrollBar().setValue(
            self.log_view.verticalScrollBar().maximum())

    def closeEvent(self, event):
        if self.thread and self.thread.isRunning():
            self.thread.stop()
        event.accept()


# ---------------------------------------------------------------------------
# 自测：无界面验证协议解析
# ---------------------------------------------------------------------------

def _make_frame(vals=None):
    vals = vals or [[r * 8 + c + 1 for c in range(8)] for r in range(8)]
    lines = ["\r\n=== Hall Matrix (8x8) ==="]
    for row in vals:
        lines.append(" ".join(f"{v:04d}" for v in row))
    lines.append("==========================")
    lines.append("")
    return "\r\n".join(lines).encode("utf-8")


def selftest():
    frame = _make_frame()
    # 1. 完整一帧
    m = _parse_from_bytes(frame)
    assert m and len(m) == 8 and m[7] == [57, 58, 59, 60, 61, 62, 63, 64], "完整帧解析错误"
    # 2. 分片跨块
    buf = bytearray(frame[:37])
    assert parse_frames(buf) == []
    buf += frame[37:]
    frames = parse_frames(buf)
    assert len(frames) == 1 and frames[0][0] == [1, 2, 3, 4, 5, 6, 7, 8], "分片解析错误"
    # 3. 多帧 + 前后杂讯
    noisy = b"hello\r\n" + frame + b"# \r\n" + frame + b"garbage"
    buf2 = bytearray(noisy)
    frames2 = parse_frames(buf2)
    assert len(frames2) == 2, f"多帧解析错误: {len(frames2)}"
    print("SELFTEST PASS: 全部解析用例通过")
    return 0


def main():
    parser = argparse.ArgumentParser(description="8x8 霍尔矩阵可视化上位机")
    parser.add_argument("--selftest", action="store_true", help="运行协议解析自测")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()

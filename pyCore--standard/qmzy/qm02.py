import cv2
import numpy as np
from datetime import datetime


class SecuritySystem:
    def __init__(self):
        # 初始化摄像头并检查是否成功打开
        self.cap = cv2.VideoCapture(0)
        if not self.cap.isOpened():
            raise ValueError("Cannot open camera")

        # 获取摄像头画面的宽度和高度，用于后续处理
        self.width = int(self.cap.get(3))
        self.height = int(self.cap.get(4))

        # 初始化限制区域相关变量
        self.roi_selected = False  # 标记是否已选择限制区域
        self.roi = []  # 存储限制区域的坐标[(x1,y1), (x2,y2)]
        self.intrusion_detected = False  # 标记是否检测到入侵
        self.detection_enabled = False  # 标记是否启用检测功能
        self.intrusion_start_time = None  # 记录入侵开始时间
        self.MIN_INTRUSION_DURATION = 2  # 最小入侵持续时间(秒)，用于过滤短暂误判

        # 初始化行人检测器（使用HOG+SVM算法）
        self.hog = cv2.HOGDescriptor()
        self.hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())

        # 创建显示窗口并设置鼠标回调函数，用于框选限制区域
        cv2.namedWindow("Intelligent Security Surveillance System")
        cv2.setMouseCallback("Intelligent Security Surveillance System", self.select_roi)

    def select_roi(self, event, x, y, flags, param):
        """鼠标回调函数：用于选择监控限制区域"""
        if event == cv2.EVENT_LBUTTONDOWN:
            # 左键按下时，记录起点坐标
            self.roi = [(x, y)]
            self.roi_selected = False
        elif event == cv2.EVENT_LBUTTONUP:
            # 左键释放时，记录终点坐标并完成区域选择
            self.roi.append((x, y))
            self.roi_selected = True
            self.detection_enabled = True  # 启用检测功能
            print(f"Restricted area set: {self.roi}")
    def detect_persons(self, frame):
        """检测画面中的行人并判断是否在限制区域内"""
        # 转换为灰度图以提高检测效率
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        # 使用HOG+SVM检测行人，返回边界框和置信度
        boxes, weights = self.hog.detectMultiScale(gray, winStride=(8, 8))
        persons = []
        for (x, y, w, h) in boxes:
            # 计算行人中心坐标
            center_x = x + w // 2
            center_y = y + h // 2
            # 检查行人是否在限制区域内
            if self.roi_selected:
                x1, y1 = self.roi[0]
                x2, y2 = self.roi[1]
                # 确保坐标按正确顺序排列（左上角和右下角）
                x_min, x_max = min(x1, x2), max(x1, x2)
                y_min, y_max = min(y1, y2), max(y1, y2)
                # 判断行人中心是否在限制区域内
                if x_min <= center_x <= x_max and y_min <= center_y <= y_max:
                    persons.append((x, y, w, h, True))  # 标记为入侵
                else:
                    persons.append((x, y, w, h, False))  # 标记为非入侵
            else:
                persons.append((x, y, w, h, False))  # 未设置限制区域时全部标记为非入侵

        return persons

    def draw_persons(self, frame, persons):
        """在画面上绘制检测到的行人，并标记是否在限制区域内"""
        intrusion_detected = False

        for (x, y, w, h, in_roi) in persons:
            if in_roi:
                # 对限制区域内的行人绘制红色边界框
                cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 0, 255), 2)
                intrusion_detected = True  # 标记检测到入侵
            else:
                # 对限制区域外的行人绘制绿色边界框
                cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)

        return frame, intrusion_detected

    def draw_roi(self, frame):
        """在画面上绘制限制区域边界框"""
        if self.roi_selected and len(self.roi) == 2:
            x1, y1 = self.roi[0]
            x2, y2 = self.roi[1]

            # 根据是否有入侵切换边界框颜色
            color = (0, 0, 255) if self.intrusion_detected else (255, 0, 0)

            # 绘制限制区域矩形
            cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)

            # 添加区域标签
            cv2.putText(frame, "Restricted Area", (x1, y1 - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.9, color, 2)

        return frame

    def should_show_alarm(self):
        """根据入侵持续时间决定是否显示警报"""
        if not self.intrusion_detected:
            return False
        if self.intrusion_start_time is None:
            # 首次检测到入侵，记录开始时间
            self.intrusion_start_time = datetime.now()
            return True
        # 计算入侵持续时间
        elapsed_time = (datetime.now() - self.intrusion_start_time).total_seconds()
        # 超过最小持续时间才显示警报，避免短暂误判
        return elapsed_time >= self.MIN_INTRUSION_DURATION
    def add_alarm_overlay(self, frame):
        """添加警报覆盖效果（红色半透明遮罩和警报文字）"""
        if self.should_show_alarm():
            # 创建红色半透明覆盖层
            overlay = frame.copy()
            cv2.rectangle(overlay, (0, 0), (self.width, self.height),
                          (0, 0, 255), -1)  # -1表示填充整个矩形

            # 将覆盖层与原始画面融合
            alpha = 0.3  # 透明度级别
            cv2.addWeighted(overlay, alpha, frame, 1 - alpha, 0, frame)

            # 添加警报文字和时间戳
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            cv2.putText(frame, f"ALERT: Restricted Area Intrusion! {timestamp}", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

        return frame

    def add_status_info(self, frame):
        """添加系统状态和操作说明信息"""
        # 添加系统状态显示
        status = "Active" if self.detection_enabled else "Inactive"
        color = (0, 255, 0) if self.detection_enabled else (0, 0, 255)

        cv2.putText(frame, f"System Status: {status}", (10, self.height - 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

        # 添加操作说明
        cv2.putText(frame, "Operation: Draw ROI with mouse | ESC to exit", (self.width - 250, self.height - 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

        return frame

    def run(self):
        """运行安全监控系统主循环"""
        print("Intelligent Security Surveillance System started")
        print("Please use the mouse to draw a restricted area")

        while True:
            # 如果没有检测到入侵，重置入侵开始时间
            if not self.intrusion_detected:
                self.intrusion_start_time = None

            # 读取一帧视频
            ret, frame = self.cap.read()
            if not ret:
                print("Cannot retrieve frame, exiting...")
                break

            # 绘制限制区域
            frame = self.draw_roi(frame)

            # 如果已设置限制区域并启用检测，则进行行人检测
            if self.detection_enabled:
                # 检测行人
                persons = self.detect_persons(frame)

                # 绘制行人并更新入侵状态
                frame, self.intrusion_detected = self.draw_persons(frame, persons)

                # 添加警报覆盖效果
                frame = self.add_alarm_overlay(frame)

            # 添加状态信息
            frame = self.add_status_info(frame)

            # 显示处理后的画面
            cv2.imshow("Intelligent Security Surveillance System", frame)

            # 按ESC键退出程序
            key = cv2.waitKey(1)
            if key == 27:  # ESC键
                break

        # 释放资源
        self.cap.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    try:
        system = SecuritySystem()
        system.run()
    except Exception as e:
        print(f"System error: {e}")
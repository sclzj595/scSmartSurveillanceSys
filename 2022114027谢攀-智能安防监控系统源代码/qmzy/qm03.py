import cv2
import numpy as np
from datetime import datetime, timedelta


class SecuritySystem:
    def __init__(self):
        """初始化智能安防监控系统"""
        # 初始化摄像头并检查是否成功打开
        self.cap = cv2.VideoCapture(0)
        if not self.cap.isOpened():
            raise ValueError("Cannot open camera")

        # 获取摄像头画面的宽度和高度
        self.width = int(self.cap.get(3))
        self.height = int(self.cap.get(4))

        # 初始化系统变量
        self.detection_enabled = False  # 控制是否启用物体检测的标志
        self.object_threshold = 300     # 最小轮廓面积阈值，用于过滤小物体(如手机、笔)
        self.stay_time_threshold = 2    # 物体被判定为遗留物所需的停留时间阈值(秒)
        self.stability_threshold = 2    # 像素移动阈值，用于判断物体是否静止
        self.frame_count = 0            # 帧计数器，用于跟踪处理的总帧数

        # 优化的背景减除器，用于检测前景物体
        # history参数控制背景模型的历史帧数，varThreshold控制像素是否属于背景的阈值
        self.bg_subtractor = cv2.createBackgroundSubtractorMOG2(
            history=100, varThreshold=50, detectShadows=False)

        # 跟踪中的物体字典，格式为{物体ID: {'轮廓': 轮廓数据, '位置': (x,y), '开始时间': 时间戳}}
        self.tracked_objects = {}
        self.next_object_id = 0  # 下一个可用的物体ID

        # 已识别的遗留物体字典，格式为{物体ID: {'轮廓': 轮廓数据, '开始时间': 时间戳}}
        self.left_objects = {}

        # 创建显示窗口
        cv2.namedWindow("Intelligent Security Surveillance System")

    def process_frame(self, frame):
        """处理每一帧图像以检测遗留物体"""
        self.frame_count += 1

        # 应用背景减除算法，获取前景掩码
        fg_mask = self.bg_subtractor.apply(frame)

        # 对前景掩码进行阈值处理，得到二值图像
        _, thresh = cv2.threshold(fg_mask, 127, 255, cv2.THRESH_BINARY)

        # 形态学操作：开运算和闭运算，减少噪声并填充孔洞
        kernel = np.ones((3, 3), np.uint8)
        thresh = cv2.morphologyEx(thresh, cv2.MORPH_OPEN, kernel, iterations=1)
        thresh = cv2.morphologyEx(thresh, cv2.MORPH_CLOSE, kernel, iterations=2)

        # 在二值图像中查找轮廓
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        current_time = datetime.now()  # 记录当前时间
        detected_objects = []  # 存储当前帧检测到的有效物体

        # 处理每个检测到的轮廓
        for contour in contours:
            # 计算轮廓面积
            area = cv2.contourArea(contour)

            # 忽略小于阈值的小轮廓，减少误报
            if area < self.object_threshold:
                continue

            # 获取边界框和质心坐标
            x, y, w, h = cv2.boundingRect(contour)
            M = cv2.moments(contour)
            if M["m00"] != 0:
                # 计算质心坐标
                cX = int(M["m10"] / M["m00"])
                cY = int(M["m01"] / M["m00"])
            else:
                # 防止除零错误，使用边界框中心
                cX, cY = x + w // 2, y + h // 2

            # 存储检测到的物体信息
            detected_objects.append({
                'contour': contour,
                'position': (cX, cY),
                'bbox': (x, y, w, h),
                'area': area
            })

        # 跟踪物体并检测遗留物品
        self.track_and_detect_left_objects(detected_objects, current_time)

        # 在帧上绘制检测结果
        self.draw_results(frame)

        return frame

    def track_and_detect_left_objects(self, detected_objects, current_time):
        """跟踪检测到的物体并识别遗留物品"""
        # 将检测到的物体与现有跟踪对象进行匹配
        for obj in detected_objects:
            matched = False
            min_dist = float('inf')
            best_match_id = None
            # 寻找最近的现有跟踪对象
            for obj_id, tracked_obj in self.tracked_objects.items():
                # 跳过已经被识别为遗留物的对象
                if obj_id in self.left_objects:
                    continue
                # 计算当前检测物体与跟踪物体之间的欧氏距离
                dist = np.sqrt(
                    (obj['position'][0] - tracked_obj['position'][0]) ** 2 +
                    (obj['position'][1] - tracked_obj['position'][1]) ** 2
                )
                # 找到距离最近且小于稳定性阈值的匹配
                if dist < min_dist and dist < self.stability_threshold:
                    min_dist = dist
                    best_match_id = obj_id
            # 更新现有跟踪或创建新跟踪
            if best_match_id is not None:
                # 更新位置、轮廓和边界框信息
                self.tracked_objects[best_match_id]['position'] = obj['position']
                self.tracked_objects[best_match_id]['contour'] = obj['contour']
                self.tracked_objects[best_match_id]['bbox'] = obj['bbox']
            else:
                # 创建新的跟踪对象
                self.tracked_objects[self.next_object_id] = {
                    'position': obj['position'],
                    'contour': obj['contour'],
                    'bbox': obj['bbox'],
                    'start_time': current_time
                }
                self.next_object_id += 1

        # 检查现有物体是否满足遗留物条件
        for obj_id, obj in list(self.tracked_objects.items()):
            if obj_id not in self.left_objects:
                # 计算物体停留时间
                stay_time = (current_time - obj['start_time']).total_seconds()
                # 如果停留时间超过阈值，则标记为遗留物
                if stay_time >= self.stay_time_threshold:
                    self.left_objects[obj_id] = {
                        'contour': obj['contour'],
                        'bbox': obj['bbox'],
                        'start_time': obj['start_time']
                    }

    def draw_results(self, frame):
        """在帧上绘制跟踪对象和遗留物体"""
        # 用红色覆盖层标记遗留物体
        for obj_id, obj in self.left_objects.items():
            x, y, w, h = obj['bbox']

            # 绘制红色矩形边界
            cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 0, 255), 2)

            # 添加红色半透明覆盖层，增强视觉警告
            overlay = frame.copy()
            cv2.rectangle(overlay, (x, y), (x + w, y + h), (0, 0, 255), -1)
            alpha = 0.3  # 透明度
            cv2.addWeighted(overlay, alpha, frame, 1 - alpha, 0, frame)

            # 添加"遗留物体"标签
            cv2.putText(frame, "LEFT OBJECT", (x, y - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

            # 在画面顶部添加全局警报信息
            cv2.putText(frame, "ALERT: LEFT BEHIND OBJECT DETECTED!", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

    def add_status_info(self, frame):
        """在帧上添加系统状态信息"""
        # 显示系统当前状态(活动/非活动)
        status = "Active" if self.detection_enabled else "Inactive"
        color = (0, 255, 0) if self.detection_enabled else (0, 0, 255)

        # 添加系统状态文本
        cv2.putText(frame, f"System Status: {status}", (10, self.height - 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)

        # 显示当前检测到的遗留物体数量
        cv2.putText(frame, f"Left Objects: {len(self.left_objects)}", (10, self.height - 40),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 0, 0), 1)

        # 显示操作提示
        cv2.putText(frame, "Press 's' to start/stop detection | ESC to exit",
                    (self.width - 300, self.height - 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

        return frame

    def run(self):
        """运行安防监控系统主循环"""
        print("Intelligent Security Surveillance System started")
        print("Press 's' to start/stop object detection")

        # 让相机预热30帧
        for _ in range(30):
            ret, _ = self.cap.read()
            if not ret:
                break

        while True:
            # 读取一帧图像
            ret, frame = self.cap.read()
            if not ret:
                print("Cannot retrieve frame, exiting...")
                break

            # 如果启用了检测功能，则处理当前帧
            if self.detection_enabled:
                frame = self.process_frame(frame)

            # 添加状态信息
            frame = self.add_status_info(frame)

            # 显示结果画面
            cv2.imshow("Intelligent Security Surveillance System", frame)

            # 处理键盘输入
            key = cv2.waitKey(1) & 0xFF

            # 按ESC键退出系统
            if key == 27:
                break

            # 按's'键切换检测功能的开关
            if key == ord('s'):
                self.detection_enabled = not self.detection_enabled
                status = "started" if self.detection_enabled else "stopped"
                print(f"Object detection {status}")

        # 释放资源
        self.cap.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    try:
        # 创建并运行安防系统
        system = SecuritySystem()
        system.run()
    except Exception as e:
        print(f"System error: {e}")
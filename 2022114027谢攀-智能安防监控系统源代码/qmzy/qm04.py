import cv2
import numpy as np
import threading
import time
import requests
from datetime import datetime
import os
import imutils
from imutils.video import VideoStream
import logging

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)


class SecuritySystem:
    def __init__(self):
        # 初始化系统参数
        self.intrusion_areas = []  # 禁区列表
        self.distance_threshold = 2.0  # 距离阈值(米)
        self.motion_threshold = 30  # 运动检测阈值
        self.record_duration = 10  # 异常事件录制时长(秒)
        self.alarm_active = False  # 报警状态
        self.output_folder = "security_footage"  # 录像保存文件夹
        self.frame_width = 640
        self.frame_height = 480

        # 创建输出文件夹
        if not os.path.exists(self.output_folder):
            os.makedirs(self.output_folder)

        # 初始化摄像头
        self.initialize_cameras()

        # 初始化人脸识别模型
        self.face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')

        # 初始化深度估计模型
        self.initialize_depth_estimation()

        # 线程控制
        self.running = False
        self.threads = []

        # 事件记录
        self.events = []

    def initialize_cameras(self):
        """初始化系统使用的摄像头"""
        try:
            # 内置摄像头
            self.camera1 = VideoStream(src=0).start()
            time.sleep(2.0)  # 等待摄像头初始化

            # 手机摄像头
            self.camera2_url = "http://admin:admin@192.168.43.1:8081/"
            self.camera2 = VideoStream(src=self.camera2_url).start()
            time.sleep(2.0)

            logger.info("摄像头初始化成功")
        except Exception as e:
            logger.error(f"摄像头初始化失败: {e}")
            raise

    def initialize_depth_estimation(self):
        """初始化深度估计模型"""
        # 使用StereoBM创建深度估计器
        self.stereo = cv2.StereoBM_create(numDisparities=16, blockSize=15)

        # 相机校准参数
        self.focal_length = 800  # 焦距(像素)
        self.baseline = 10  # 基线距离(厘米)

        logger.info("深度估计模型初始化成功")

    def add_intrusion_area(self, camera_id, area_coords):
        """添加禁区
        Args:
            camera_id: 摄像头ID
            area_coords: 禁区坐标[(x1,y1), (x2,y2), (x3,y3), (x4,y4)]
        """
        self.intrusion_areas.append({
            "camera_id": camera_id,
            "coords": np.array(area_coords, np.int32),
            "alert": False
        })
        logger.info(f"已添加禁区到摄像头 {camera_id}")

    def detect_faces(self, frame):
        """人脸检测
        Args:
            frame: 输入帧
        Returns:
            检测到的人脸列表[(x,y,w,h)]
        """
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        faces = self.face_cascade.detectMultiScale(
            gray,
            scaleFactor=1.1,
            minNeighbors=5,
            minSize=(30, 30),
            flags=cv2.CASCADE_SCALE_IMAGE
        )
        return faces
    def calculate_disparity(self, left_frame, right_frame):
        """计算视差图
        Args:
            left_frame: 左摄像头帧
            right_frame: 右摄像头帧
        Returns:
            视差图
        """
        # 转换为灰度图
        left_gray = cv2.cvtColor(left_frame, cv2.COLOR_BGR2GRAY)
        right_gray = cv2.cvtColor(right_frame, cv2.COLOR_BGR2GRAY)
        # 计算视差图
        disparity = self.stereo.compute(left_gray, right_gray)

        # 归一化以便显示
        disparity = cv2.normalize(disparity, None, alpha=0, beta=255, norm_type=cv2.NORM_MINMAX, dtype=cv2.CV_8U)

        return disparity

    def calculate_distance(self, disparity):
        """根据视差计算距离
        Args:
            disparity: 视差值
        Returns:
            距离(米)
        """
        if disparity > 0:
            distance = (self.focal_length * self.baseline) / (disparity * 100)  # 转换为米
            return distance
        return float('inf')

    def detect_motion(self, frame, prev_frame):
        """运动检测
        Args:
            frame: 当前帧
            prev_frame: 前一帧
        Returns:
            运动区域轮廓列表
        """
        if prev_frame is None:
            return []

        # 转换为灰度图并进行高斯模糊
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        gray = cv2.GaussianBlur(gray, (21, 21), 0)

        prev_gray = cv2.cvtColor(prev_frame, cv2.COLOR_BGR2GRAY)
        prev_gray = cv2.GaussianBlur(prev_gray, (21, 21), 0)

        # 计算帧差
        frame_delta = cv2.absdiff(prev_gray, gray)
        thresh = cv2.threshold(frame_delta, 25, 255, cv2.THRESH_BINARY)[1]

        # 扩展阈值图像填充孔洞
        thresh = cv2.dilate(thresh, None, iterations=2)

        # 寻找轮廓
        contours = cv2.findContours(thresh.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        contours = imutils.grab_contours(contours)

        # 过滤小轮廓
        motion_contours = []
        for contour in contours:
            if cv2.contourArea(contour) < self.motion_threshold:
                continue
            motion_contours.append(contour)

        return motion_contours

    def detect_intrusion(self, frame, motion_contours, camera_id):
        """检测禁区入侵
        Args:
            frame: 当前帧
            motion_contours: 运动区域轮廓
            camera_id: 摄像头ID
        Returns:
            是否有入侵, 入侵区域
        """
        intrusion_detected = False
        intrusion_contours = []

        # 获取当前摄像头的禁区
        camera_areas = [area for area in self.intrusion_areas if area["camera_id"] == camera_id]

        for contour in motion_contours:
            for area in camera_areas:
                # 检查轮廓是否在禁区内
                area_points = area["coords"]

                # 提取轮廓的第一个点并转换为正确格式
                try:
                    # 确保点是浮点数元组
                    point = tuple(contour[0][0].astype(float))

                    # 进行多边形测试
                    result = cv2.pointPolygonTest(area_points, point, False)

                    if result >= 0:  # 点在多边形内
                        intrusion_detected = True
                        intrusion_contours.append(contour)

                        # 更新禁区状态
                        if not area["alert"]:
                            area["alert"] = True
                            self.trigger_alarm(f"禁区入侵检测 - 摄像头 {camera_id}")
                except Exception as e:
                    logger.error(f"多边形测试错误: {e}")
                    continue

        return intrusion_detected, intrusion_contours

    def process_camera_feed(self, camera_id, camera_stream, output_name):
        """处理摄像头视频流
        Args:
            camera_id: 摄像头ID
            camera_stream: 摄像头流对象
            output_name: 输出视频文件名
        """
        prev_frame = None
        writer = None
        alert_end_time = 0

        # 创建视差图窗口
        cv2.namedWindow(f"Disparity - Camera {camera_id}")

        while self.running:
            try:
                # 读取帧
                frame = camera_stream.read()

                if frame is None:
                    logger.warning(f"摄像头 {camera_id} 无法读取帧")
                    time.sleep(0.1)
                    continue

                # 调整帧大小
                frame = cv2.resize(frame, (self.frame_width, self.frame_height))

                # 复制原始帧用于显示
                display_frame = frame.copy()

                # 人脸检测
                faces = self.detect_faces(frame)
                for (x, y, w, h) in faces:
                    cv2.rectangle(display_frame, (x, y), (x + w, y + h), (0, 255, 0), 2)
                    cv2.putText(display_frame, "Face", (x, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
                # 运动检测
                motion_contours = self.detect_motion(frame, prev_frame)
                prev_frame = frame.copy()
                # 绘制运动区域
                for contour in motion_contours:
                    (x, y, w, h) = cv2.boundingRect(contour)
                    cv2.rectangle(display_frame, (x, y), (x + w, y + h), (0, 255, 255), 2)
                # 禁区检测
                intrusion_detected, intrusion_contours = self.detect_intrusion(frame, motion_contours, camera_id)
                # 绘制并标注禁区
                for area in [a for a in self.intrusion_areas if a["camera_id"] == camera_id]:
                    color = (0, 0, 255) if area["alert"] else (0, 255, 0)
                    cv2.polylines(display_frame, [area["coords"]], True, color, 2)
                    # 计算禁区边界框
                    x, y, w, h = cv2.boundingRect(area["coords"])
                    # 计算文本位置（方框顶部中心）
                    text_size = cv2.getTextSize("Restricted Area", cv2.FONT_HERSHEY_SIMPLEX, 0.7, 2)[0]
                    text_x = x + (w - text_size[0]) // 2  # 水平居中
                    text_y = y - 10  # 垂直位置在方框上方10像素
                    # 确保文本不会超出图像边界
                    if text_y < 10:
                        text_y = y + h + 20  # 如果上方空间不足，显示在方框下方
                    # 标注禁区
                    cv2.putText(display_frame, "Restricted Area", (text_x, text_y),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

                # 如果检测到入侵且有另一个摄像头可用，进行深度估计
                disparity = None
                if intrusion_detected and camera_id == 1 and self.camera2 is not None:
                    camera2_frame = self.camera2.read()
                    if camera2_frame is not None:
                        camera2_frame = cv2.resize(camera2_frame, (self.frame_width, self.frame_height))

                        # 计算视差图
                        disparity = self.calculate_disparity(frame, camera2_frame)

                        # 显示视差图
                        if disparity is not None:
                            disparity_display = cv2.applyColorMap(disparity, cv2.COLORMAP_JET)
                            cv2.imshow(f"Disparity - Camera {camera_id}", disparity_display)

                # 对入侵区域计算平均视差和距离
                if intrusion_detected and disparity is not None:
                    for contour in intrusion_contours:
                        mask = np.zeros_like(disparity)
                        cv2.drawContours(mask, [contour], -1, 255, -1)

                        # 计算平均视差
                        masked_disparity = cv2.bitwise_and(disparity, disparity, mask=mask)
                        valid_pixels = masked_disparity[mask > 0]

                        if len(valid_pixels) > 0:
                            avg_disparity = np.mean(valid_pixels)
                            distance = self.calculate_distance(avg_disparity)

                            # 获取轮廓中心
                            M = cv2.moments(contour)
                            if M["m00"] != 0:
                                cX = int(M["m10"] / M["m00"])
                                cY = int(M["m01"] / M["m00"])

                                # 只有当距离小于阈值时才触发报警
                                if distance < self.distance_threshold:
                                    cv2.putText(display_frame, f"距离: {distance:.2f}m", (cX, cY),
                                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

                                    # 记录事件
                                    event_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                                    self.events.append({
                                        "time": event_time,
                                        "camera_id": camera_id,
                                        "type": "intrusion",
                                        "distance": distance
                                    })

                # 显示实时距离信息
                if intrusion_detected and disparity is not None:
                    cv2.putText(display_frame, f"Closest Target Distance: {distance:.2f}m", (10, 30),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

                # 显示帧
                cv2.imshow(f"Camera {camera_id}", display_frame)

                # 处理报警状态和录像
                if intrusion_detected:
                    # 开始录制
                    if writer is None:
                        fourcc = cv2.VideoWriter_fourcc(*"MJPG")
                        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                        output_path = os.path.join(self.output_folder, f"{output_name}_{timestamp}.avi")
                        writer = cv2.VideoWriter(output_path, fourcc, 20.0, (self.frame_width, self.frame_height))
                        logger.info(f"开始录制事件: {output_path}")

                    # 重置报警结束时间
                    alert_end_time = time.time() + self.record_duration

                # 继续录制直到报警结束
                if writer is not None:
                    writer.write(display_frame)
                    if time.time() > alert_end_time:
                        writer.release()
                        writer = None
                        logger.info("停止录制事件")

                # 按'q'键退出
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    self.running = False
                    break

            except Exception as e:
                logger.error(f"处理摄像头 {camera_id} 时出错: {e}")
                time.sleep(0.1)

        # 释放资源
        if writer is not None:
            writer.release()
        cv2.destroyWindow(f"Disparity - Camera {camera_id}")

    def trigger_alarm(self, message):
        """触发报警
        Args:
            message: 报警消息
        """
        self.alarm_active = True
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # 记录报警事件
        self.events.append({
            "time": timestamp,
            "type": "alarm",
            "message": message
        })

        logger.warning(f"报警触发: {message}")

        # 这里可以添加声音报警、短信通知等功能

        # 5秒后解除报警
        threading.Timer(5.0, self.reset_alarm).start()

    def reset_alarm(self):
        """重置报警状态"""
        self.alarm_active = False
        # 重置所有禁区的报警状态
        for area in self.intrusion_areas:
            area["alert"] = False
        logger.info("报警已解除")

    def start(self):
        """启动安全监控系统"""
        self.running = True

        # 启动摄像头处理线程
        camera1_thread = threading.Thread(target=self.process_camera_feed,
                                          args=(1, self.camera1, "camera1"))
        camera2_thread = threading.Thread(target=self.process_camera_feed,
                                          args=(2, self.camera2, "camera2"))

        self.threads.append(camera1_thread)
        self.threads.append(camera2_thread)

        # 启动线程
        for thread in self.threads:
            thread.start()

        logger.info("智能安防监控系统已启动")

    def stop(self):
        """停止安全监控系统"""
        self.running = False

        # 等待所有线程结束
        for thread in self.threads:
            thread.join()

        # 释放摄像头资源
        if self.camera1 is not None:
            self.camera1.stop()

        if self.camera2 is not None:
            self.camera2.stop()

        # 关闭所有OpenCV窗口
        cv2.destroyAllWindows()

        logger.info("智能安防监控系统已停止")

    def get_event_logs(self):
        """获取事件日志"""
        return self.events


# 主函数
if __name__ == "__main__":
    try:
        # 创建安全系统实例
        security_system = SecuritySystem()

        # 设置禁区 - 示例坐标，根据实际情况调整
        security_system.add_intrusion_area(1, [(100, 100), (200, 100), (200, 200), (100, 200)])
        security_system.add_intrusion_area(2, [(150, 150), (250, 150), (250, 250), (150, 250)])

        # 设置距离阈值(米)
        security_system.distance_threshold = 1.5

        # 启动系统
        security_system.start()

        # 等待用户输入终止
        input("按Enter键停止系统...\n")

    except Exception as e:
        logger.critical(f"系统错误: {e}")
    finally:
        # 确保系统停止
        if 'security_system' in locals() and security_system.running:
            security_system.stop()
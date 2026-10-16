import cv2
import os
import numpy as np
from datetime import datetime


class SmartSecuritySystem:
    def __init__(self):
        # 初始化人脸识别器和分类器
        self.face_detector = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')
        self.recognizer = cv2.face.LBPHFaceRecognizer_create()
        self.face_samples = []
        self.ids = []
        self.id_name_map = {}  # ID到姓名的映射
        self.current_id = 0

        # 初始化摄像头
        self.cap = cv2.VideoCapture(0)
        if not self.cap.isOpened():
            raise ValueError("无法打开摄像头")

        # 初始化日志文件
        self.log_file = "security_log.txt"
        self.init_log_file()

    def init_log_file(self):
        """初始化日志文件"""
        with open(self.log_file, "w") as f:
            f.write(f"智能安防监控系统日志 - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write("=" * 50 + "\n")

    def log_event(self, event_type, message):
        """记录事件到日志文件"""
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        log_entry = f"[{timestamp}] [{event_type}] {message}\n"
        print(log_entry.strip())

        with open(self.log_file, "a") as f:
            f.write(log_entry)

    def prepare_dataset(self, dataset_path):
        """从数据集路径准备训练数据"""
        if not os.path.exists(dataset_path):
            os.makedirs(dataset_path)
            self.log_event("INFO", f"创建数据集目录: {dataset_path}")
            return False

        self.log_event("INFO", f"开始加载数据集: {dataset_path}")

        for person_name in os.listdir(dataset_path):
            person_dir = os.path.join(dataset_path, person_name)
            if not os.path.isdir(person_dir):
                continue

            self.current_id += 1
            self.id_name_map[self.current_id] = person_name
            self.log_event("INFO", f"加载人物: {person_name} (ID: {self.current_id})")

            for img_name in os.listdir(person_dir):
                img_path = os.path.join(person_dir, img_name)
                if not img_path.lower().endswith(('.png', '.jpg', '.jpeg')):
                    continue

                img = cv2.imread(img_path, 0)  # 以灰度模式读取
                faces = self.face_detector.detectMultiScale(img, 1.3, 5)

                if len(faces) == 1:
                    (x, y, w, h) = faces[0]
                    self.face_samples.append(img[y:y + h, x:x + w])
                    self.ids.append(self.current_id)
                else:
                    self.log_event("WARNING", f"无法从 {img_path} 中检测到唯一人脸")

        if len(self.face_samples) > 0:
            self.log_event("INFO", f"成功加载 {len(self.face_samples)} 个人脸样本")
            return True
        else:
            self.log_event("ERROR", "未找到有效的人脸样本")
            return False

    def train_model(self):
        """训练人脸识别模型"""
        if not self.face_samples:
            self.log_event("ERROR", "没有人脸样本可用于训练")
            return False

        self.log_event("INFO", "开始训练人脸识别模型...")
        self.recognizer.train(self.face_samples, np.array(self.ids))
        self.log_event("INFO", "模型训练完成")
        return True

    def start_monitoring(self):
        """开始实时监控"""
        if not self.face_samples:
            self.log_event("ERROR", "未训练模型，无法开始监控")
            return

        self.log_event("INFO", "开始实时监控...")
        self.log_event("INFO", "按 'q' 键退出监控")

        while True:
            ret, frame = self.cap.read()
            if not ret:
                self.log_event("ERROR", "无法获取视频帧")
                break

            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            faces = self.face_detector.detectMultiScale(
                gray,
                scaleFactor=1.2,
                minNeighbors=5,
                minSize=(20, 20)
            )

            for (x, y, w, h) in faces:
                cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 0, 0), 2)
                face_roi = gray[y:y + h, x:x + w]

                # 识别人脸
                id_predicted, confidence = self.recognizer.predict(face_roi)

                # 设置置信度阈值
                if confidence < 100:
                    person_name = self.id_name_map.get(id_predicted, "Unknown")
                    confidence_text = f"{round(100 - confidence)}%"
                    cv2.putText(frame, f"{person_name} ({confidence_text})",
                                (x + 5, y - 5), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)

                    # 记录识别事件
                    if person_name != "Unknown":
                        self.log_event("RECOGNITION", f"识别到 {person_name} (置信度: {confidence_text})")
                else:
                    cv2.putText(frame, "Unknown", (x + 5, y - 5),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
                    self.log_event("RECOGNITION", "识别到未知人员")

            # 显示当前时间
            cv2.putText(frame, datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                        (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

            # 显示帧
            cv2.imshow('智能安防监控系统', frame)

            # 按 'q' 键退出
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

        self.log_event("INFO", "停止监控")
        self.cap.release()
        cv2.destroyAllWindows()

    def add_new_person(self, person_name, dataset_path, num_samples=30):
        """添加新人员到数据集"""
        person_dir = os.path.join(dataset_path, person_name)
        if not os.path.exists(person_dir):
            os.makedirs(person_dir)
            self.log_event("INFO", f"创建新人员目录: {person_dir}")
        else:
            self.log_event("WARNING", f"人员目录已存在: {person_dir}")

        self.log_event("INFO", f"开始为 {person_name} 采集人脸样本...")
        self.log_event("INFO", f"请面对摄像头，系统将采集 {num_samples} 张人脸样本")

        count = 0
        while count < num_samples:
            ret, frame = self.cap.read()
            if not ret:
                self.log_event("ERROR", "无法获取视频帧")
                break

            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            faces = self.face_detector.detectMultiScale(
                gray,
                scaleFactor=1.2,
                minNeighbors=5,
                minSize=(20, 20)
            )

            for (x, y, w, h) in faces:
                cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 0, 0), 2)
                count += 1

                # 保存人脸样本
                img_path = os.path.join(person_dir, f"{person_name}_{count}.png")
                cv2.imwrite(img_path, gray[y:y + h, x:x + w])
                cv2.putText(frame, f"采集样本: {count}/{num_samples}",
                            (50, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 0, 0), 2)

            cv2.imshow('采集人脸样本', frame)

            # 按 'q' 键提前退出
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

        self.log_event("INFO", f"成功为 {person_name} 采集 {count} 张人脸样本")
        cv2.destroyAllWindows()
        return count > 0


# 使用示例
if __name__ == "__main__":
    # 数据集路径
    DATASET_PATH = "face_dataset"

    # 创建安防系统实例
    security_system = SmartSecuritySystem()

    # 检查是否需要添加新人员
    add_new = input("是否添加新人员到数据集？(y/n): ").lower()
    while add_new == 'y':
        person_name = input("请输入人员姓名: ")
        security_system.add_new_person(person_name, DATASET_PATH)
        add_new = input("是否添加另一个人员？(y/n): ").lower()

    # 准备数据集并训练模型
    if security_system.prepare_dataset(DATASET_PATH):
        security_system.train_model()

        # 开始监控
        security_system.start_monitoring()
    else:
        print("无法准备数据集，程序退出")
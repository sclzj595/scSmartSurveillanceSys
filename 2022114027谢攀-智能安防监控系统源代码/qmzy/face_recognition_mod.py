import cv2
import os
import numpy as np

class FaceRecModule:
    def __init__(self, face_dir='faces'):
        self.face_dir = face_dir
        self.face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')
        self.recognizer = cv2.face.LBPHFaceRecognizer_create()
        self.names = []
        self._train()

    def _train(self):
        images, labels = [], []
        for i, name in enumerate(os.listdir(self.face_dir)):
            img_path = os.path.join(self.face_dir, name)
            img = cv2.imread(img_path, cv2.IMREAD_GRAYSCALE)
            if img is not None:
                images.append(img)
                labels.append(i)
                self.names.append(os.path.splitext(name)[0])
        if images:
            self.recognizer.train(images, np.array(labels))

    def run(self):
        cap = cv2.VideoCapture(0)
        while True:
            ret, frame = cap.read()
            if not ret:
                break
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            faces = self.face_cascade.detectMultiScale(gray, 1.2, 4)
            for (x, y, w, h) in faces:
                face_img = gray[y:y+h, x:x+w]
                try:
                    label, confidence = self.recognizer.predict(face_img)
                    name = self.names[label]
                    color = (0,255,0) if confidence < 70 else (0,0,255)
                    cv2.putText(frame, name, (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2)
                except:
                    cv2.putText(frame, 'Unknown', (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0,0,255), 2)
                cv2.rectangle(frame, (x, y), (x+w, y+h), (255,255,0), 2)
            cv2.imshow('Face Recognition', frame)
            if cv2.waitKey(1) & 0xFF == 27:
                break
        cap.release()
        cv2.destroyAllWindows()